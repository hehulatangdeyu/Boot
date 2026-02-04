#ifndef __NORFLASH_H
#define __NORFLASH_H

void norflash_init(void);
bool bl_verify_external_norflash_firmware(void);
int nor_flash_erase_download_slot(void);
int nor_flash_program_download_slot(uint32_t address, uint32_t size, uint8_t *src);
int nor_flash_program_meta_slot(meta_desc_info_t *meta);
uint32_t download_slot_verify(uint32_t address, uint32_t size);
int download_slot_to_intflash(void);
int select_slot_to_active_backup_partition(int flag);

#endif  