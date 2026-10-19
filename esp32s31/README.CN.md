# ESP32-S31 DS5 Dongle

独立的 ESP-IDF 工程，目标板为 ESP-Mosaico 的 ESP32-S31。协议行为以原版 `ca1af99` 为基准，板载屏幕独立显示电脑连接、手柄连接和电量，不依赖 ESP-Mosaico-Claw。

支持 DualSense 与 Edge、原版 Sony USB HID 描述符和 VID/PID；透传摇杆、按键、触摸板、运动传感器和电量，以及普通震动、灯条、玩家灯和自适应扳机输出。Edge 使用原版配置文件解锁、预取、读写及保存后快照刷新时序。

已接入原版四声道 48kHz USB 音频：前两声道供扬声器/耳机，后两声道供 HD 震动。包含 WDL 重采样、Opus 扬声器编码与麦克风解码。麦克风按原版复制为双声道。HD 震动需要游戏发送触觉音频；关闭扬声器不会删除 HD 震动音频接口。

F6–F9 配置工具接口包括配置校验/保存、USB 重连、版本、真实蓝牙 RSSI 和音频状态。设置包括 250Hz/500Hz/实时轮询、DS5/Edge/自动模式、USB 序列号、空闲断开、音量及锁定、震动增益、扳机功率、音频缓冲、麦克风/扬声器开关。唤醒和 PS 快捷键关闭时不枚举键盘；打开后 PS 短按发送 Win+G、长按发送 Win+Tab。变更接口相关设置后需重连 USB。唤醒支持 S3，原版不支持 S0 Modern Standby。

电脑持续挂起约 3 秒后关闭手柄；瞬时挂起和主动 USB 重连会取消该操作。屏幕次要状态行显示电量和充电标记，放电且电量不高于 10% 时小字交替变色，代替 Pico LED 的低电量提示。

## 编译

使用 Espressif ESP-IDF 6.2，并在 PowerShell 中运行：

```powershell
git submodule update --init --recursive
idf.py -B build-full build
```

构建输出：`build-full/ds5_dongle_s31.bin`。可用 `idf.py -B build-full -p COM9 flash` 经 ROM 下载模式烧录，或用 esptool 按 `build-full/flash_args` 指定的地址烧录。运行时没有串口。不要清空 NVS，否则会丢失配对和设置。已有旧版 sdkconfig 时需将 `CONFIG_FREERTOS_HZ` 设为 `1000`、`CONFIG_ESP_MAIN_TASK_STACK_SIZE` 设为 `16384`；首次配置由 sdkconfig.defaults 提供。此固件只适用于 ESP32-S31。

## 接线与使用

- USB 数据线连接设备与主机，S31 会作为 Sony DualSense HID 手柄出现。
- 手柄按住 `PS + Create` 进入配对状态；连接后主机可直接使用手柄。
- 启动开启 30 秒扫描，并优先连接最近成功连接的手柄；配对信息保存在 NVS，已配对的手柄也可主动重连。
- Edge 配置资料首次读取时需要等待控制器完成解锁和预取，配置工具应在重试读取时使用。

## BOOT 键

使用板卡定义的 GPIO61、低电平有效：

| 操作 | 行为 |
| --- | --- |
| 单击 | 已连接时断开当前手柄；未连接时开始 30 秒配对扫描 |
| 双击 | 重启固件 |
| 三击 | 重启至 ESP32 ROM 下载模式 |
| 长按约 1.5 秒 | 断开并删除全部配对，保存拒绝自动重连名单 |

重新接入已清除的手柄时，单击 BOOT 开始扫描，再按住手柄 PS + Create 进入配对模式；仅按 PS 不会自动接回。原版 Pico 的 UF2 存储盘烧录在 S31 上替换为 ROM 下载模式。

## 验证与源码

`python tests/run_host_tests.py` 使用 g++ 编译实际协议模块，测试 USB 速度/型号/功能开关组合、输出状态、震动标志、Edge 时序、配置校验和持久化。这些测试不替代 Windows 枚举、游戏 HD 震动、音频、PS Accessories、S3 唤醒和按键的硬件验证。

`main/upstream/` 保存原版适配模块，来源及差异见目录内 README。`app_s31.cpp` 负责蓝牙事件和主循环，`config_s31.cpp` 负责 NVS，`ds5_bridge.c` 负责 Bluedroid，`usb_speed_s31.cpp` 补充高速限定符，`s31_platform.h` 对接 FreeRTOS，`ds5_display.c` 负责圆屏。仓库根 `src/` 的 Pico 工程没有改动。
