#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include "flash_area.h"
#include "meta_desc.h"
#include "bl_button.h"
#include "bl_led.h"
#include "bl_uart.h"
#include "bitos.h"
#include "norflash.h"
#include "hpatchlite.h"

LOG_MODULE_REGISTER(boot, CONFIG_LOG_DEFAULT_LEVEL);

#define BL_BOOT_MTU_SIZE         4096
#define BL_MAX_TRANSFER_MTUSIZE  (BL_BOOT_MTU_SIZE + 8)
#define BL_BOOT_VERSION          "v1.0.1"

typedef enum
{
    IDLE,
    FWPSN,
    START,
    OPCODE,
    DATA,
    VERIFY
} bl_state_machine_t;

typedef enum
{
    BL_OPCODE_NONE,
    OPCODE_INQUIRY = 0x10,
    OPCODE_QUERY = 0x40,
    OPCODE_READ = 0x12,
    OPCODE_PROGRAM = 0x20,
    OPCODE_ERASE = 0x21,
    OPCODE_VERIFY = 0x22,
    OPCODE_RESET = 0x81,
    OPCODE_BOOT = 0x82,
    OPCODE_UNKNOWN = 0xFF
} bl_opcode_t;

typedef enum
{
    BL_ERR_OK = 0,
    BL_ERR_OVERFLOW,
    BL_ERR_UNKNOWN,
} bl_response_err_t;

typedef struct
{
    bl_state_machine_t state;
    bl_opcode_t opcode;
    uint32_t length;
    uint32_t index;
    uint16_t crc;
    uint8_t data[5112];
} bl_ctrl_t;

typedef enum
{
    BL_INQUIRY_VERSION,
    BL_INQUIRY_MTU_SIZE
} bl_inquiry_t;

typedef struct
{
    uint32_t expect_app_address;
    uint32_t expect_arg_address;
} bl_query_info_t;

typedef struct
{
    uint8_t subcode;
} bl_inquiry_info_t;

typedef struct
{
    uint32_t address;
    uint32_t size;
} bl_erase_info_t;

typedef struct
{
    uint32_t address;
    uint32_t size;
    uint8_t data[]; 
} bl_program_info_t;

typedef struct
{
    uint32_t address;
    uint32_t size;
    uint32_t crc;
} bl_verify_info_t;

static meta_desc_info_t meta_desc;
static meta_desc_info_t *meta = &meta_desc;

static uint8_t response_buf[4104];
static device_flash_info_t device_flash_info;
static bl_ctrl_t packet;
static bl_ctrl_t *pkt = &packet;

void goto_app_main(void)
{
    volatile uint32_t *app_vt = (uint32_t*)device_flash_info.app_base_addr;
    uint32_t app_msp = app_vt[0];
    void (*app_main)(void) = (void (*)(void))app_vt[1];

    LOG_WRN("goto app main: PC: 0x%08x, SP: 0x%08x", app_vt[1], app_msp);

    k_sleep(K_MSEC(20));
    irq_lock();

    __ISB();
    __set_MSP(app_msp);
    __set_PSP(app_msp);
    __DSB();
    __ISB();

    app_main();

    while (1);
}

static void bl_response(bl_response_err_t err, bl_opcode_t opcode, uint8_t *data, uint16_t length)
{
    uint8_t *packet = response_buf;
    uint8_t *ptr = packet;
    const uint8_t header = 0xAA;
    put_u8_inc(&ptr, header); // Start byte
    put_u8_inc(&ptr, opcode);
    put_u8_inc(&ptr, err);
    put_u16_inc(&ptr, length); // Length placeholder
    put_bytes_inc(&ptr, data, length); // Data
    uint16_t crc = crc16_itu_t(0, packet, ptr - packet);
    put_u16_inc(&ptr, crc);
    
    bl_upgrade_packet_send(response_buf, ptr - packet);
}

static void bl_response_ack(bl_opcode_t opcode)
{
    bl_response(BL_ERR_OK, opcode, NULL, 0);
}

static void bl_query_handler(void)
{
    LOG_DBG("query state");

    bl_query_info_t* query = (bl_query_info_t*)&pkt->data[4];
    if (pkt->length != sizeof(bl_query_info_t))
    {
        LOG_ERR("query param length mismatch, expected: %u, got: %u", 
            sizeof(bl_query_info_t), pkt->length);
        bl_response(BL_ERR_UNKNOWN, OPCODE_QUERY, NULL, 0);
        return;
    }

    if (query->expect_arg_address != device_flash_info.arg_base_addr)
    {
        LOG_ERR("query expect arg address faild, expected: 0x%08x, got: 0x%08x",
               device_flash_info.arg_base_addr, query->expect_arg_address);
        bl_response(BL_ERR_UNKNOWN, OPCODE_QUERY, NULL, 0);
        return;
    }

    if (query->expect_app_address != device_flash_info.app_base_addr)
    {
        LOG_ERR("query expect app address faild, expected: 0x%08x, got: 0x%08x",
               device_flash_info.app_base_addr, query->expect_app_address);
        bl_response(BL_ERR_UNKNOWN, OPCODE_QUERY, NULL, 0);
        return;
    }

    LOG_INF("query expect app addr: 0x%08x, arg addr: 0x%08x",
           query->expect_app_address, query->expect_arg_address);
    
    bl_response(BL_ERR_OK, OPCODE_QUERY, (uint8_t*)CONFIG_BOARD, sizeof(CONFIG_BOARD) - 1);
}

static void bl_inquiry_handler(void)
{
    LOG_DBG("inquiry state");

    bl_inquiry_info_t* inquiry = (bl_inquiry_info_t*)&pkt->data[4];
    LOG_DBG("inquiry subcode: 0x%02x", inquiry->subcode);
    switch (inquiry->subcode)
    {
        case BL_INQUIRY_VERSION:
        {
            bl_response(BL_ERR_OK, OPCODE_INQUIRY, (uint8_t*)BL_BOOT_VERSION, strlen(BL_BOOT_VERSION));
            break;
        }
        case BL_INQUIRY_MTU_SIZE:
        {
            uint16_t boot_size = BL_MAX_TRANSFER_MTUSIZE;
            bl_response(BL_ERR_OK, OPCODE_INQUIRY, (uint8_t*)&boot_size, sizeof(boot_size));
            break;
        }
    }
}

static void bl_reset_handler(void)
{
    LOG_DBG("reset state");

    NVIC_SystemReset();
}

static void bl_boot_handler(void)
{
    LOG_DBG("boot state");

    bl_response_ack(OPCODE_BOOT);

    int check = ota_update_task();
    int ret = 0;
    if (check != 0 && check == FULL_PACKAGE_FLAG)
    {
        ret = download_slot_to_intflash();
        if (ret != 0) {
            LOG_ERR("download slot to int flash error: %d", ret);
            return;
        }
        LOG_INF("full package update success!"); // 全量更新时从download分区备份
        ret = select_slot_to_active_backup_partition(FULL_PACKAGE_FLAG);

    } else if (check == DIFF_PACKAGE_FLAG) {
        ret = select_slot_to_active_backup_partition(DIFF_PACKAGE_FLAG);
    }

    if (ret != 0) {
        LOG_ERR("active back error");
    }

    goto_app_main();
}

static void bl_erase_handler(void)
{
    LOG_DBG("erase state");
    bl_erase_info_t* erase = (bl_erase_info_t*)&pkt->data[4];
    if (pkt->length != sizeof(bl_erase_info_t))
    {
        LOG_ERR("erase it param faild");
        bl_response(BL_ERR_UNKNOWN, OPCODE_ERASE, NULL, 0);
        return;
    }

    if (erase->address >= device_flash_info.boot_base_addr && 
        erase->address < (device_flash_info.boot_base_addr + device_flash_info.boot_flash_size))
    {
        LOG_ERR("it addr not erase");
        bl_response(BL_ERR_UNKNOWN, OPCODE_ERASE, NULL, 0);
        return;
    }

    if (erase->address >= device_flash_info.app_base_addr &&
        erase->address + erase->size <= device_flash_info.app_base_addr + device_flash_info.app_flash_size)
    {
        meta->firmware_addr = erase->address;
        meta->firmware_size = erase->size;
        strcpy(meta->firmware_version, BL_BOOT_VERSION);

        int ret;
        ret = nor_flash_erase_download_slot();
        if (ret != 0) {
            bl_response(BL_ERR_UNKNOWN, OPCODE_ERASE, NULL, 0);
            return;
        }

        bl_response_ack(OPCODE_ERASE);    // erase successful
    }

    else if (erase->address >= device_flash_info.arg_base_addr &&
             erase->address + erase->size <= device_flash_info.arg_base_addr + device_flash_info.arg_flash_size)
    {
        int ret = bl_flash_erase(erase->address, erase->size);
        if (ret != 0) {
            bl_response(BL_ERR_UNKNOWN, OPCODE_ERASE, NULL, 0);
            return;
        }

        bl_response_ack(OPCODE_ERASE);
    }
}

static void bl_program_handler(void)
{
    LOG_DBG("program state");

    bl_program_info_t* program = (bl_program_info_t*)&pkt->data[4];
    if (pkt->length != sizeof(bl_program_info_t) + program->size)
    {
        LOG_ERR("program it param faild");
        bl_response(BL_ERR_UNKNOWN, OPCODE_PROGRAM, NULL, 0);
        return;
    }

    if (program->address >= device_flash_info.boot_base_addr && 
        program->address < (device_flash_info.boot_base_addr + device_flash_info.boot_flash_size))
    {
        LOG_ERR("it addr not write");
        bl_response(BL_ERR_UNKNOWN, OPCODE_PROGRAM, NULL, 0);
        return;
    }

    if (program->address >= device_flash_info.app_base_addr &&
        program->address + program->size <= device_flash_info.app_base_addr + device_flash_info.app_flash_size)
    {
        meta->download_len += program->size;
        meta->firmware_state = NEW;
        meta->is_program = 1;

        int ret;
        ret = nor_flash_program_download_slot(program->address, program->size, program->data);
        if (ret != 0)
        {
            bl_response(BL_ERR_UNKNOWN, OPCODE_PROGRAM, NULL, 0);
            return;
        }

        ret = nor_flash_program_meta_slot(meta);
        if (ret != 0)
        {
            LOG_ERR("backup meta is faild");
            return;
        }

        bl_response_ack(OPCODE_PROGRAM);
    }

    else if(program->address >= device_flash_info.arg_base_addr &&
            program->address + program->size <= device_flash_info.arg_base_addr + device_flash_info.arg_flash_size)
    {
        int ret = bl_flash_program(program->address, program->size, program->data);
        if (ret != 0)
        {
            bl_response(BL_ERR_UNKNOWN, OPCODE_PROGRAM, NULL, 0);
            return;
        }

        bl_response_ack(OPCODE_PROGRAM);
    }
}

static void bl_verify_handler(void)
{
    bl_verify_info_t* verify = (bl_verify_info_t*)&pkt->data[4];
    if (pkt->length != sizeof(bl_verify_info_t))
    {
        LOG_ERR("verify it param faild, expected: %u, got: %u", sizeof(bl_verify_info_t), pkt->length);
        return;
    }

    uint32_t crc = 0;
    if (verify->address >= device_flash_info.app_base_addr &&
        verify->address + verify->size <=
        device_flash_info.app_base_addr + device_flash_info.app_flash_size)
    {
        crc = (uint32_t)download_slot_verify(verify->address, verify->size);
        if (crc != verify->crc)
        {
            LOG_ERR("verify faild: expected 0x%08x, got 0x%08x", crc, verify->crc);
            return;
        }

        LOG_INF("verify success, crc 0x%08x", crc);

        int ret;
        // 此时download分区已经完成应有工作，将meta_a分区中的download_len刷写成0, 由于上位机尚未实现, 逻辑功能不完整
        // 应在握手前检查download分区已有数据的CRC，校验通过执行断点续传, 校验失败重传所有数据
        meta_desc.download_len = 0;
        ret = nor_flash_program_meta_slot(meta);
        if (ret != 0)
            return;

        bl_response_ack(OPCODE_VERIFY); // verify successful
    }
    else if(verify->address >= device_flash_info.arg_base_addr &&
            device_flash_info.arg_base_addr + device_flash_info.arg_flash_size)
    {
        crc = (uint32_t)crc32_ieee((const uint8_t *)verify->address, (size_t)verify->size);
        if (crc != verify->crc)
        {
            LOG_ERR("verify faild: expected 0x%08x, got 0x%08x", crc, verify->crc);
            return;
        }

        LOG_INF("verify success, crc 0x%08x", crc);
        bl_response_ack(OPCODE_VERIFY); // verify successful
    }
}

bool bl_pkt_handler(void)
{
    switch (pkt->opcode)
    {
        case OPCODE_QUERY:
        {
            bl_query_handler();
            return true;
        }
        case OPCODE_INQUIRY:
        {
            bl_inquiry_handler();
            return true;
        }
        case OPCODE_ERASE:
        {
            bl_erase_handler();
            return true;
        }
        case OPCODE_READ:
        {
            return true;
        }
        case OPCODE_PROGRAM:
        {
            bl_program_handler();
            return true;
        }
        case OPCODE_RESET:
        {
            bl_reset_handler();
            return true;
        }
        case OPCODE_BOOT:
        {
            bl_boot_handler();
            return true;
        }
        case OPCODE_VERIFY:
        {
            bl_verify_handler();
            return true;
        }
        case OPCODE_UNKNOWN:
        {
            return false;
        }
        default: return false;
    }
}

void bl_pkt_reset(void)
{
    pkt->state = IDLE;
    pkt->opcode = OPCODE_UNKNOWN;
    pkt->length = 0;
    pkt->index = 0;
    pkt->crc = 0;
    memset(pkt->data, 0, sizeof(pkt->data));
}

bool bl_received_handler(uint8_t data)
{
    pkt->data[pkt->index++] = data;
    switch (pkt->state)
    {
        case IDLE:
        {
            if (data == 0xAA) // Start byte
            {
                pkt->state = START;
            }
            else
            {
                bl_pkt_reset();
            }
            break;
        }
        case START:
        {
            pkt->opcode = (bl_opcode_t)pkt->data[1];
            pkt->state = OPCODE;
            break;
        }
        case OPCODE:
        {
            if (pkt->index == 4)
            {
                pkt->length = *(uint16_t*)&pkt->data[2];
                if (pkt->length > sizeof(pkt->data) - 4)
                {
                    LOG_ERR("pkt Length faild");
                    bl_pkt_reset();
                }
                if (pkt->length == 0) pkt->state = VERIFY;
                else                  pkt->state = DATA;
            }
            break;
        }
        case DATA:
        {
            if (pkt->index >= pkt->length + 4)
            {
                pkt->state = VERIFY;
            }
        }
        case VERIFY:
        {
            if (pkt->index == pkt->length + 6)
            {
                pkt->crc = *(uint16_t*)&pkt->data[pkt->length + 4];
                uint32_t ccrc = crc16_itu_t(0, pkt->data, pkt->length + 4);
                if (pkt->crc != ccrc)
                {
                    LOG_ERR("parse pkt crc faild, got 0x%08x, expected 0x%08x", pkt->crc, ccrc);
                    bl_pkt_reset();
                    return false;
                }
                return true;
            }
            break;
        }
        case FWPSN:
        {
            break;
        }
        default: break;
    }
    return false;
}

void bl_print_log(void)
{
    LOG_DBG("packet info: header: 0x%02x", pkt->data[0]);
    LOG_DBG("packet info: opcode: 0x%02x", pkt->opcode);
    LOG_DBG("info: length: %u", pkt->length);
    LOG_DBG("info: data:");
    if (pkt->length > 0) 
    {
         for (uint32_t i = 0; i < pkt->length; i++)
        {
            printk ("%02x ", pkt->data[i + 4]);
        }
    }
    LOG_DBG("crc: 0x%08x", pkt->crc);
}

static bool bl_verify_internal_flash_firmware(void)
{
    uint32_t ccrc = 0, fwaddr = 0, fwsize = 0, fwcrc = 0; 
    bool check;
    check = bl_flash_get_arginfo(&fwaddr, &fwsize, &fwcrc);
    if (check)
    {
        LOG_DBG("Debug: arg info: address 0x%08x, size %u, crc 0x%08x", fwaddr, fwsize, fwcrc);
        ccrc = crc32_ieee((const uint8_t *)fwaddr, (size_t)fwsize);
        if (ccrc != fwcrc)
        {
            LOG_ERR("arg flash verify faild: expected 0x%08x, got 0x%08x", fwcrc, ccrc);
            return false; // Verify faild
        }
        LOG_INF("arg flash verify success: addr 0x%08x, size %u, crc 0x%08x", fwaddr, fwsize, ccrc);
        return true;
    }
    else
    {
        LOG_ERR("flash call: arg flash info not found!");
        return false; // verify faild
    }
}

bool bl_verify_firmware(void)
{
    if (bl_verify_internal_flash_firmware())
        return true;

    if (bl_verify_external_norflash_firmware()) // check active backup partition crc correct, program int flash
        return true;

    return false;
}
  
void boot_main(bool trap)
{
    get_device_flash_info(&device_flash_info);

    int ret = k_sem_take(&button_trap, K_SECONDS(3));
    if (ret == 0)
        trap = true;
    
    trap ? NULL : goto_app_main();

    LOG_WRN("trap boot wait upgrading");
}

void sys_init_thread(void *p1, void *p2, void *p3)
{
    LOG_INF("system init started...");
    bl_button_init();
    bl_led_init();
    bl_upgrade_uart_init();
    norflash_init();
    bl_verify_firmware() ? boot_main(false) : boot_main(true);
}

K_THREAD_DEFINE(sys_init_id, 2048, sys_init_thread, NULL, NULL, NULL, 8, 0, 0);
