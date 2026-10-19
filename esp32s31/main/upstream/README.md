# 原版模块来源与适配

本目录以 `awalol/DS5Dongle` 提交 `ca1af99` 的 `src/` 为来源，保留各文件原始版权及许可证声明。根 LICENSE 仍适用；audio.cpp 的 MPL-2.0、usb_descriptors.cpp 的 MIT 声明保留。

保留音频通道分离与报文、Opus 参数、输出状态合并、Edge 时序、快捷键、HID 配置接口和 Sony HID 报告描述符。平台差异：

- Pico 时间、队列、互斥和多核接口改为 s31_platform.h 的 FreeRTOS 实现；编解码循环增加一次 tick 的 CPU 让出。
- utils.h 去掉与协议无关的 BTstack HCI 名称工具，并增加头文件保护。
- USB 高速的音频 1ms 间隔和 HID 250/500/1000Hz 间隔换算；键盘端点移到 0x85，高速轮询为 8ms（全速保留原版 10ms）。
- C++ 使用 static_assert 校验键盘描述符长度。
- wake.cpp 保留原版状态机及 Linux 下的 DCD 唤醒回退逻辑。

配置持久化、蓝牙主机、BOOTSEL、LED 和主循环由父目录的 S31 代码提供。之前额外添加的通用电量 HID 报告已移除，电量通过原版报告上报，屏幕解析同一原始字节。
