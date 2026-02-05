概述

本项目是一个支持全量及差分升级的嵌入式 Bootloader 系统，采用模块化设计，集成 HPatchLite 差分算法，提供可靠、高效的固件更新方案，并具备离线自恢复能力。

更新历史

2026-01-26

新增上位机 HPatchLite 差分升级功能。

2026-02-05

修复 bitos.h 文件中的字节序操作宏，改为使用更标准的 byteoder.h。
修复原因：在 ARM 架构中，若指针地址不是 4 的整数倍，访问 uint32_t 会直接触发 HardFault 硬件错误；原版本未处理大小端字节序问题。修复后架构支持更灵活。

修复 bl_uart.c 与 work_queue.c 中的数据接收逻辑，采用标准 ringbuf 管理 bin 文件数据存取，并明确区分 bl_uart.c 中的线程优先级，以规避竞态情况。
修复逻辑：高优先级的 upgrade rx thread 在通知数据包收满后，交由 packet rx thread 处理。为保证 Flash 擦除操作时 CPU 不被 upgrade rx thread 抢占，避免形成竞态。

修复 bl_button.c 中硬编码的 disable_gpio_interrupts API 函数，改用标准的 Zephyr 管理方式，移除按键回调与引脚配置的硬编码。

升级流程

上位机 → 下发期望存储固件信息地址、期望固件存储起始地址 <-> 下位机 → Ack

上位机 → 下发查询 Bootloader 版本号 <-> 下位机 → Ack + "v1.0.1" + MTU Size

上位机 → 根据解析的 xbin 文件下发擦除命令、擦除地址、擦除大小 <-> 下位机 → Ack + 根据参数执行擦除 Flash 动作

上位机 → 按帧格式打包下发固件数据 <-> 下位机 → 写入 NorFlash 的 download slot

...（重复下发与写入）

上位机 → 下发最后一包数据 <-> 下位机 → 写入最后一包数据

上位机 → 计算固件 CRC32 并下发校验值、固件大小、固件起始地址 <-> 下位机 → 手动计算 download slot 分区中固件的 CRC32（使用 CRC32 IEEE 标准）并与接收值比对

上位机 → 擦除命令，准备下发固件 MAGIC、FWSIZE、FWCRC、FWADDR（共 16 字节）<-> 下位机 → 接收并将 16 字节写入 arg info 分区

上位机 → 校验这 16 字节是否写入成功（四个参数的校验值）<-> 下位机 → 校验

上位机 → boot 命令 <-> 下位机 → 接收，解析 download 分区中的前 64 字节信息（包括新固件大小、CRC 等），依据前四字节 MAGIC 判断是全量数据包还是差分数据包：

全量数据包：执行 download slot → internal flash 的写入

差分数据包：利用 download slot 中的新固件与 internal flash 中的旧固件，通过 HPatchLite → tinyuz 解压缩还原，写入 diff fw slot；写入成功后，再将 diff fw slot 数据搬运到 internal flash，并检查 CRC 匹配情况

依据返回值决定从 download slot 分区还是 diff fw slot 分区备份固件到 active backup slot（实现离线自恢复能力）

最后跳转至 app_main

数据包帧格式

字段				长度				说明

header				1 byte				帧头

package number		2 bytes				包序号

operation code		1 byte				操作码

data length			2 bytes				数据长度

data				data length bytes	数据载荷

crc16				2 bytes	CRC16 		校验

最大传输单元（MTU）：4104 字节（含帧头、序号、操作码、长度、数据与 CRC）

相关开源组件
HPatchLite：https://github.com/sisong/HPatchLite.git
用于差分固件的生成与还原。

Tinyuz：https://github.com/sisong/tinyuz.git
轻量级解压缩库，配合 HPatchLite 实现差分升级的解压过程。
