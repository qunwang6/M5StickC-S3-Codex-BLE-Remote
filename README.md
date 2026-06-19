# M5StickC S3 Codex BLE Remote

M5StickC S3 通过 BLE 模拟蓝牙键盘，用来控制 macOS 命令行里的 Codex 确认界面。

## 功能

- 蓝牙设备名：`CodexBtn-S3`
- 中间按钮 / Button A：发送 `Enter`
- 右侧按钮 / Button B：发送键盘 `Down Arrow`
- 外接 `GPIO 0` 按钮：发送 `Enter`
- 屏幕水平显示，顶部左侧显示蓝牙图标和状态，顶部右侧显示电量图标和百分比
- 电量定时刷新
- 按住右侧按钮开机：清除 M5StickC S3 保存的蓝牙配对记录

## 使用方法

1. 烧录固件到 M5StickC S3。
2. 在 macOS 打开 `System Settings -> Bluetooth`。
3. 连接蓝牙设备 `CodexBtn-S3`。
4. 打开 Terminal 或 iTerm，并让 Codex 命令行窗口保持在前台。
5. 按 M5StickC S3 中间按钮发送 `Enter`，按右侧按钮发送向下选择。

## 命令行编译

```bash
cd M5StickC-S3-Codex-BLE-Remote
PLATFORMIO_CORE_DIR=.platformio-core pio run
```

## 命令行烧录

自动选择串口：

```bash
cd M5StickC-S3-Codex-BLE-Remote
PLATFORMIO_CORE_DIR=.platformio-core pio run -t upload
```

指定串口：

```bash
cd M5StickC-S3-Codex-BLE-Remote
PLATFORMIO_CORE_DIR=.platformio-core pio run -t upload --upload-port /dev/cu.usbmodem101
```

如果串口不是 `/dev/cu.usbmodem101`，先查看当前串口：

```bash
ls /dev/cu.*
```

## Arduino IDE

也可以打开 `ESP32S3_Codex_BLE_Button.ino`。这个文件只作为 Arduino IDE 入口，实际代码在 `src/main.cpp`，避免两份代码不同步。

需要安装：

- `esp32 by Espressif Systems`
- `ESP32 BLE Keyboard`
- `NimBLE-Arduino`
- `M5Unified`

## 蓝牙重新配对

如果 macOS 曾经配对过旧固件，重启后无法自动连接或手动连接失败，按下面步骤重新配对：

1. macOS 蓝牙设置里找到 `CodexBtn-S3`。
2. 选择 `Forget This Device`。
3. 按住 M5StickC S3 右侧按钮，然后开机，屏幕出现 `Bonds cleared`。
4. 重新在 macOS 蓝牙设置里连接 `CodexBtn-S3`。

## 当前代码结构

- `src/main.cpp`：主程序
- `ESP32S3_Codex_BLE_Button.ino`：Arduino IDE 入口
- `platformio.ini`：PlatformIO 配置

当前 PlatformIO 构建使用 `USE_NIMBLE`，用于 BLE HID 键盘连接。

