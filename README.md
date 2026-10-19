# DS5Dongle-ESP32S31

面向 ESP-Mosaico ESP32-S31 的独立 DualSense 无线适配器固件。

移植自 [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)，协议基准提交为 `ca1af99`。本仓库仅包含 S31 工程、编译必需的 WDL / Opus 子模块及许可证，不包含 Pico 工程。

支持 DualSense / Edge、普通震动与 HD 震动、自适应扳机、灯光、扬声器 / 耳机 / 麦克风、网页配置、USB 唤醒、PS 快捷键，以及板载屏幕连接状态和电量显示。功能已接入并通过编译和主机协议测试，硬件兼容性仍需继续验证。

## 编译

使用支持 ESP32-S31 的 Espressif ESP-IDF 6.2 环境：

```powershell
git clone --recurse-submodules https://github.com/oAhihZniy/DS5Dongle-ESP32S31.git
cd DS5Dongle-ESP32S31/esp32s31
idf.py -B build-full build
```

详细使用、烧录和按键说明见 [S31 工程说明](esp32s31/README.CN.md)。原始版权与许可证声明保留，参见 [LICENSE](LICENSE) 及各源文件声明。
