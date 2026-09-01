# ESP32-S3-Touch-LCD-1.54 导航 UI

微雪 [ESP32-S3-Touch-LCD-1.54](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.54) 的 PlatformIO 工程。界面按官方 Brookesia 出厂固件的交互来做：桌面图标、底边上滑回桌面、停在中部打开最近任务。

## 开发板配置

来源：官方仓库 `examples/ESP32-S3-Touch-LCD-1.54-demo` 的 BSP 与 Arduino 示例。

| 项目 | 规格 |
| --- | --- |
| MCU | ESP32-S3R8，双核 240 MHz |
| 存储 | 16 MB Flash + 8 MB OPI PSRAM |
| 屏幕 | 1.54 寸 IPS，240×240，ST7789，4 线 SPI |
| 触摸 | CST816，I2C |
| 其它 | QMI8658 六轴、ES8311/ES7210 音频、TF 卡、锂电池充电 |

### GPIO

| 功能 | GPIO |
| --- | --- |
| LCD DC / CS / SCLK / MOSI / RST / BL | 45 / 21 / 38 / 39 / 40 / 46 |
| 触摸 INT / RST | 48 / 47 |
| I2C SDA / SCL | 42 / 41 |
| I2S MCLK / BCLK / WS / DIN / DOUT | 8 / 9 / 10 / 11 / 12 |
| 功放 PA | 7 |
| SDMMC CLK/CMD/D0–D3 | 16 / 15 / 17 / 18 / 13 / 14 |
| 电池 ADC / EN / 充电检测 | 1 / 2 / 3 |
| BOOT / PLUS / PWR | 0 / 4 / 5 |

LCD SPI 使用 **mode 3**、40 MHz，颜色反相（IPS），与官方 `esp_lcd_new_panel_st7789` 一致。

## 交互（对齐官方 Brookesia）

- 出厂壁纸 + App 图标（从微雪 `06_esp-brookesia.bin` 抽出），状态栏用官方 Brookesia 20×20 资源
- 每页两个 60×60 图标（容器 90×90），第一页：AIChats / Settings
- 顶栏居中：Wi-Fi、时间 `HH.MM`（配网后 NTP 校时，时区 `CST-8`）、电池
- Settings → **WIFI** → **wifi config**：扫码或加入 `ESP32S3_1_54` / `12345678`，浏览器打开 `192.168.4.1` 填家里的 Wi-Fi
- 左右滑动切换页面，底部分页点（当前页 40×12，其余 12×12）
- **从屏幕底部上滑**：退出当前 App，回到桌面
- **从底部上滑并停在屏幕中部**：打开最近任务
- 最近任务里再上滑：结束该 App
- 点最近任务卡片：回到该 App

内置 App：AIChats（占位）、Settings、Photos / Music / Video（需 TF 卡）、Analyzer、Gravity（QMI8658）、Squareline。

官方完整固件（相册 / 音乐 / 视频 / 小智）在 ESP-IDF 的 `06_esp-brookesia` 示例里，依赖 Brookesia 组件；本工程用 LVGL 9 在 PlatformIO 里复现同一套导航。

## 实体按键规则

| 位置 | 名称 | GPIO | 单击 | 双击 | 长按 |
| --- | --- | ---: | --- | --- | --- |
| 左侧 | BOOT | 0 | 返回桌面 | 打开最近任务 | 保留，不调整音量 |
| 中间 | PWR / 音量减 | 5 | 音量降低 10% | 无独立功能 | 每 180 ms 连续降低 10% |
| 右侧 | PLUS / 音量加 | 4 | 音量提高 10% | 无独立功能 | 每 180 ms 连续提高 10% |

按键行为约束：

- BOOT 专用于系统导航，不与音量功能复用。
- 音量减和音量加在桌面、应用及设置子页面全局生效，范围为 0%–100%。
- 调整音量时显示全局悬浮条，停止操作 900 ms 后自动隐藏。
- 音量变化停止 1.5 秒后写入 NVS，避免长按期间频繁擦写 Flash；设备重启后恢复上次音量。
- GPIO 8 和 GPIO 10 分别用于 I2S MCLK、WS，禁止配置为按键输入，否则会破坏音频总线。
- 原系统资源编号 8、10 分别表示音量减、音量加动画；本工程将这两个逻辑功能映射到 GPIO 5、GPIO 4 实体键。

## 项目结构

```text
src/
├── main.cpp                 # 初始化与主循环
├── hal.cpp / hal.h          # LCD、触摸、IMU、电池等硬件适配
├── wifi_svc.cpp / .h        # Wi-Fi、配网和时间服务
├── services/
│   └── volume_service.*      # 全局音量、持久化与音频输出回调
├── ui.cpp / ui.h            # 桌面、状态栏、导航、手势和最近任务
├── ui/
│   ├── ui_components.*      # 卡片、标题、开关、进度条等通用组件
│   ├── debug_overlay.*      # 全局性能悬浮层
│   └── volume_overlay.*     # 全局音量悬浮提示
├── apps/
│   ├── app_context.h        # App 可使用的系统能力
│   ├── app_registry.*       # App 元数据与生命周期注册
│   ├── aichats/
│   ├── settings/
│   ├── photos/
│   ├── music/
│   ├── video/
│   ├── analyzer/
│   ├── gravity/
│   └── squareline/
└── assets/                  # 图片与图标资源
```

每个 App 通过 `create`、`update`、`destroy` 生命周期接入系统。页面对象和运行状态归对应 App 目录管理；跨 App 复用的视觉元素放入 `src/ui/`，硬件与网络能力继续由 `hal` 和 `wifi_svc` 提供。新增 App 时创建独立子目录，并在 `app_registry.cpp` 注册，桌面无需增加分支判断。

## 编译与烧录

需要 [PlatformIO Core](https://platformio.org/install/cli) 或 Cursor/VS Code 的 PlatformIO 插件。

```bash
pio run
pio run -t upload
pio device monitor
```

若识别不到串口，按住 **BOOT** 再插 USB，松开后烧录。烧完按一次复位。

Arduino 内核使用 pioarduino **53.03.13**（对应官方文档的 Arduino 3.2.x）。USB CDC 已打开，串口波特率 115200。
