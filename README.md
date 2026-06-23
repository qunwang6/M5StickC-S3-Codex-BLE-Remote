# M5StickC S3 Codex BLE Remote

M5StickC S3 通过 BLE 模拟蓝牙键盘，用来控制 macOS 命令行里的 Codex 确认界面。

## 功能

- 蓝牙设备名：`CodexBtn-S3`
- 开机后自动启动蓝牙；短按左侧按钮亮屏不会主动断开或重连蓝牙
- 中间按钮 / Button A：发送 `Enter`
- 右侧按钮 / Button B：发送键盘 `Down Arrow`
- 长按右侧按钮 / Button B：打开菜单，可重连 BLE 或打开 Wi-Fi 配置热点
- M5StickC S3 内置按钮按 M5Unified 的 StickS3 映射读取：Button A = `GPIO11`，Button B = `GPIO12`
- 外接 `GPIO 0` 按钮：发送 `Enter`
- 屏幕水平显示，顶部左侧显示蓝牙图标和状态，顶部右侧显示电量图标和百分比
- 电量定时刷新
- 30 秒无按键操作后自动熄屏，蓝牙保持连接
- 设备正面朝下时自动息屏

## 使用方法

1. 烧录固件到 M5StickC S3。
2. 在 macOS 打开 `System Settings -> Bluetooth`，连接蓝牙设备 `CodexBtn-S3`。
4. 打开 Terminal 或 iTerm，并让 Codex 命令行窗口保持在前台。
5. 按 M5StickC S3 中间按钮发送 `Enter`，按右侧按钮发送向下选择，长按右侧按钮打开菜单。
6. 30 秒无按键操作或把设备正面朝下会自动息屏，蓝牙不会断开。

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

## 当前代码结构

- `src/main.cpp`：主程序
- `ESP32S3_Codex_BLE_Button.ino`：Arduino IDE 入口
- `platformio.ini`：PlatformIO 配置

当前 PlatformIO 构建使用 `USE_NIMBLE`，用于 BLE HID 键盘连接。

## Wi-Fi 配置

Wi-Fi 菜单会打开设备自己的配置热点，电脑连接后可以在浏览器里搜索附近 Wi-Fi 并保存账号密码。

使用步骤：

1. 长按右侧按钮打开菜单。
2. 用右侧按钮移动到 `WiFi setup`，按中间按钮选择。
3. 设备屏幕显示 `AP 192.168.4.1` 后，在电脑上连接 Wi-Fi 热点 `CodexBtn-Setup`。
4. 浏览器打开 `http://192.168.4.1/`。
5. 页面会扫描附近 Wi-Fi，选择目标网络，输入密码，点击 `Save and Connect`。

配置会保存到 NVS `Preferences` 命名空间 `codex-remote`：

- `wifi_on`：是否开机自动连接
- `wifi_ssid`：目标 Wi-Fi 名称
- `wifi_pass`：目标 Wi-Fi 密码

SSID 和密码不会默认写进仓库。如果仍想在固件里放默认凭据，也可以在 `platformio.ini` 的 `build_flags` 里启用并修改这两行：

```ini
build_flags =
  -DUSE_NIMBLE
  -DWIFI_SSID=\"你的WiFi名称\"
  -DWIFI_PASSWORD=\"你的WiFi密码\"
```

固件会优先读取 `Preferences` 里保存的配置；没有保存配置时，才使用上面的编译宏默认值。

## License

MIT License.
