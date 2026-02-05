历史:
2026 - 01 - 26
1. 新增上位机HPatchLite差分升级功能。
2026 - 02 - 05
1. 修复bitos.h文件中字节序操作宏，使用更加标准的byteoder.h [在ARM架构中，如果指针地址不是4的整数倍，访问uin32_t会直接触发HardFault硬件错误，且原版本未处理大小端字节序问题，使修复后支持的架构更灵活]。
2. 修复bl_uart.c与wrok_queue.c中数据接收逻辑，采用标准ringbuf管理bin文件数据存取，明确区分bl_uart.c中的线程优先级，规避出现竞态情况。[高优先级，upgrade rx thread 线程一旦通知数据包收满，交由packet rx thread 线程处理时, 为保证Flash擦除操作时,cpu不被upgrade rx thread 线程抢占，避免形成一种竞态情况]。
3. 修复bl_button.c中硬编码disable_gpio_interrupts硬编码API函数，使用标准的Zephyr管理方式，移除按键回调与引脚配置。

升级流程图：
上位机：下发期望存储固件信息地址、期望固件存储起始地址 ->
下位机：Ack
上位机：下发查询Bootloader版本号->
下位机：Ack + "v1.0.1" + MTU Size
上位机：根据解析的xbin文件下发擦除命令、擦除地址、擦除大小 ->
下位机：Ack + 根据解析的参数执行擦除flash动作
上位机：按帧格式打包下发固件数据 ->
下位机：写入norflash的download slot
    ......
上位机：下发最后一包数据->
下位机：写入最后一包数据
上位机：计算固件的CRC32并下发校验值、固件大小、固件起始地址 ->
下位机：手动计算一遍download slot分区中固件CRC32值与接收到的进行比对，此处使用的CRC32IEEE
上位机：擦除命令，准备下发固件MAGIC、FWSIZE、FWCRC、FWADDR ->
下位机：接收并将这16个字节写入到arg info分区
上位机：校验这16个字节是否写入成功，四个参数的校验值 ->
下位机：校验
上位机：boot命令
下位机：接收，解析download分区中的前64字节信息(包括新固件大小、新固件CRC等)，依据前四字节的MAGIC判断是全量数据包还是差分数据包
        全量数据包：执行download slot 写到 internal flash
        差分数据包：利用download slot中的新固件与internal flash中的旧固件进行HPatchLite -> tinyuz 的解压缩还原 写到 diff fw slot                    写入成功后，在将diff fw slot的数据搬运到内部的flash，最终检查内部flash的crc匹配情况
        依据返回值执行从download slot分区备份固件还是从diff fw slot分区备份固件到active backup slot(离线自恢复能力)
        goto app_main

数据包帧格式:
    | header | package number | opration code | data length |       data       | crc16 |
    | 1byte  | 2byte          | 1byte         | 2byte       | data length byte | 2byte |
    |                              MAX MTU 4104                                        |

HPatchLite开源组件:https://github.com/sisong/HPatchLite.git
Tinyuz开源组件:https://github.com/sisong/tinyuz.git
