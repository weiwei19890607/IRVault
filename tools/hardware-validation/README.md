# StickS3 硬件验证工具

这里保存 IRVault 开发初期使用的独立 ESPHome 硬件探针。它们不是正式固件，
不包含产品 UI、资料存储或 Home Assistant 红外命令实体。源码中的
`Phase0` 日志标签为历史兼容标识。

详细人工验收项目见 [中文测试清单](TEST_CHECKLIST_ZH.md)。

## 测试配置

| 配置 | 用途 |
|---|---|
| `test_a.yaml` | ESPHome 启动、USB 日志、Wi-Fi、API、OTA、Flash、PSRAM |
| `test_b.yaml` | M5Unified/M5PM1 共存与 LCD 静态画面 |
| `test_c.yaml` | GPIO11/GPIO12 电平、A/B 映射与同时按键 |
| `test_d.yaml` | ESPHome `remote_receiver` 对照实验 |
| `test_d_official_rmt.yaml` | 仿官方流程的原生 ESP-IDF RMT RX 探针 |
| `test_e_official_tx.yaml` | 仿官方流程的原生 RMT TX 电气探针 |

正式 IRVault 最终采用 M5Unified/M5PM1 薄适配层和原生 ESP-IDF RMT
RX/TX；`test_d.yaml` 只作为历史对照。

## macOS 准备

可复用仓库根目录的虚拟环境：

```bash
cd /absolute/path/to/IRVault
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
cd tools/hardware-validation
cp secrets.example.yaml secrets.yaml
```

填写本目录的 `secrets.yaml`。它同样受仓库根目录 `.gitignore` 保护。

## 编译、刷写与日志

以下以 Test A 为例：

```bash
esphome config test_a.yaml
esphome compile test_a.yaml
ls /dev/cu.usb*
esphome upload test_a.yaml --device /dev/cu.usbmodemXXXX
esphome logs test_a.yaml --device /dev/cu.usbmodemXXXX
```

请把端口替换为本机实际结果。若正常 USB 模式无法刷写，按住 StickS3 侧键，
直到内部绿色 LED 闪烁进入下载模式，再重试。

## 已确认的实机结论

- `esp32-s3-devkitc-1` 加显式 8 MB Flash / Octal PSRAM 配置可启动；
- 实机识别到 8 MB Flash 和 8 MB PSRAM；
- M5Unified 0.2.18、M5PM1、ESPHome Wi-Fi/API/OTA 可共存；
- LCD 采用 M5Unified/M5GFX 初始化可靠；
- GPIO11 为 A、GPIO12 为 B，均为 active-low，可独立读取；
- 扬声器功放可通过 M5PM1 GPIO3 关闭，IR 电源域可开启；
- 内置 IR RX/TX 分别使用 GPIO42/GPIO46；
- 原生 ESP-IDF RMT 比 ESPHome 通用 `remote_receiver` 更适合本设备；
- IR RX 为解调输出，不能测量原信号载波；正式固件默认 38 kHz。

运行硬件探针会覆盖设备上的正式固件。验证完成后，需要从仓库根目录重新
刷入 `irvault.yaml`。
