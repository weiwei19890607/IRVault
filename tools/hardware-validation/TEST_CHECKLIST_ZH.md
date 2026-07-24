# StickS3 硬件验证中文测试清单

这些配置只验证 M5Stack StickS3 与 ESPHome 的硬件可行性，不是正式
IRVault 固件。源码和预期日志中的 `Phase0` 是保留的历史标签。

## 测试前准备

1. 使用支持数据传输的 USB-C 线连接 StickS3 与 Mac。
2. 在仓库根目录激活虚拟环境，再进入本目录：

   ```bash
   cd /absolute/path/to/IRVault
   source .venv/bin/activate
   cd tools/hardware-validation
   ```

3. 确认 ESPHome 版本：

   ```bash
   esphome version
   ```

   要求为 2026.1.0 或更高、低于 2027。

4. 从模板创建本地 secrets 文件：

   ```bash
   cp secrets.example.yaml secrets.yaml
   ```

5. 填入真实 Wi-Fi 信息，并分别设置新的 API、OTA 和 fallback 密钥。API 密钥可这样生成：

   ```bash
   openssl rand -base64 32
   ```

6. 不要把 `secrets.yaml` 提交到版本库、截图公开或粘贴到聊天中。项目的 `.gitignore` 已忽略该文件。
7. 整个验证过程建议保持 USB-C 连接。Test D 会开启 5V 输出域；此时不要从 Grove 或 EXT_5V 输出端反向输入 5V，只能使用 USB-C 或官方指定的 Hat2-Bus `5VIN` 输入。

## 每项测试通用步骤

先检查配置，再编译、上传和查看串口日志。以下以 Test A 为例：

```bash
esphome config test_a.yaml
esphome compile test_a.yaml
ls /dev/cu.usb*
esphome upload test_a.yaml --device /dev/cu.usbmodemXXXX
esphome logs test_a.yaml --device /dev/cu.usbmodemXXXX
```

把 `/dev/cu.usbmodemXXXX` 换成 `ls` 显示的真实端口。

如果首次上传无法连接，按住机身侧键，直到内部绿色 LED 闪烁并进入下载模式，然后重新执行 `upload`。

注意：实机测试显示，短按这个侧键可能直接触发关机，再短按才重新开机。它不是可依赖的普通应用 reset 键。需要重启测试时，优先断开 USB-C、等待 5 秒后重新连接；测试侧键行为时单独记录结果。

Test A 成功联网后，可以通过 OTA 切换测试：

```bash
esphome run test_b.yaml --device irvault-phase0.local
```

如果 `.local` 名称无法解析，继续使用 USB 串口，或从路由器/Home Assistant 中查找设备 IP。

## Test A：ESPHome 基础能力

目标：验证启动、USB 串口日志、Wi-Fi、Home Assistant Native API、OTA、8 MB Flash 和 8 MB Octal PSRAM。

- [ ] 串口 logger 使用 ESP32-S3 的 `USB_SERIAL_JTAG`，不是 `USB_CDC`。
- [ ] `esphome config test_a.yaml` 成功。
- [ ] `esphome compile test_a.yaml` 成功。
- [ ] 首次通过 USB 成功刷入。
- [ ] 串口日志出现 `Selected Test A`。
- [ ] 芯片被识别为 ESP32-S3，核心数为 2。
- [ ] Flash 检测值为 `8388608` bytes。
- [ ] PSRAM 检测值为 `8388608` bytes。
- [ ] 没有 PSRAM 初始化失败、Flash 读取错误、brownout 或循环重启。
- [ ] Wi-Fi 成功连接。
- [ ] 若测试网络为隐藏 SSID，配置中已设置 `hidden: true`，启动时不再反复出现 `No networks found`/`RETRY_HIDDEN`。
- [ ] 设备通过 Native API 出现在 Home Assistant。
- [ ] 连续在线 30 分钟，Wi-Fi、API 和日志保持正常。
- [ ] 完整断电再上电 10 次，每次都能正常启动。
- [ ] 对 Test A 自身执行一次 OTA 更新并成功重启。
- [ ] OTA 后串口日志和 API 仍然正常。

预期关键日志：

```text
[I][sticks3.phase0] [Phase0] Selected Test A
[I][sticks3.phase0] [Phase0:A] Chip model=ESP32-S3 ... cores=2
[I][sticks3.phase0] [Phase0:A] Flash detected=8388608 bytes (expected 8388608)
[I][sticks3.phase0] [Phase0:A] PSRAM detected=8388608 bytes (expected 8388608)
[I][wifi] WiFi Connected!
[I][api] Successfully connected to ...
```

通过标准：以上项目全部成功。Flash 或 PSRAM 容量不匹配必须记为失败，不能忽略。

## Test B：M5Unified、M5PM1 与 LCD

目标：验证 M5Unified/M5GFX、内置 M5PM1 驱动和静态 LCD，同时确认它们没有破坏 ESPHome 网络功能。

- [ ] 编译并刷入 `test_b.yaml`。
- [ ] 日志显示 `StickS3=YES`。
- [ ] 日志显示 `M5PM1 integrated type_ok=YES begin=YES`。
- [ ] LCD 显示完整的 `IRVault / Phase 0 B / M5Unified/M5GFX / LCD static test`。
- [ ] 文字没有被裁切或错位。
- [ ] 屏幕方向正确。
- [ ] 颜色正常，没有反色。
- [ ] 没有闪烁、花屏或随机损坏。
- [ ] 连续在线 30 分钟，Wi-Fi、API、日志和 OTA 仍然响应。
- [ ] 重启 10 次，每次 LCD 都能稳定初始化。

预期关键日志：

```text
[I][sticks3.phase0] [Phase0] M5Unified board id=... StickS3=YES
[I][sticks3.phase0] [Phase0] M5PM1 integrated type_ok=YES begin=YES
[I][sticks3.phase0] [Phase0:B] Static LCD test drawn through M5Unified/M5GFX
```

通过标准：板型、M5PM1、LCD 和 ESPHome 网络能力全部稳定。若仅 LCD 失败但网络正常，保留日志和屏幕照片，之后单独评估 ESPHome 原生 ST7789 路线。

## Test C：两个应用按键

目标：实验确定 KEY1/KEY2 的有效电平、上下拉需求、物理 A/B 对应关系以及双键同时按下是否可靠。

初次测试保持：

```yaml
button_pull: NONE
```

- [ ] 编译并刷入 `test_c.yaml`。
- [ ] 记录完全不按键时的 KEY1/KEY2 电平。
- [ ] 单独按下并松开第一个应用按键，记录是 GPIO11 还是 GPIO12 变化。
- [ ] 单独按下并松开第二个应用按键，记录对应 GPIO。
- [ ] 根据实际机身位置记录哪个是 A、哪个是 B，不要只凭 GPIO 名称猜测。
- [ ] 根据空闲与按下电平确定 active-high 或 active-low。
- [ ] 同时按住两个按键 5 秒；两个值必须同时变化并稳定保持。
- [ ] 重复双键同时按下 20 次，不能出现只识别一个键的情况。
- [ ] 无按键时观察 5 分钟，确认电平不随机跳变。
- [ ] 如果 `NONE` 下有漂移或抖动，把配置改为 `UP` 后重新测试。
- [ ] 如仍不可靠，再改为 `DOWN` 测试。
- [ ] 最终记录能提供稳定空闲电平和明确按下变化的 pull 模式。

预期日志形式：

```text
[I][sticks3.phase0] [Phase0:C] GPIO11=KEY1 GPIO12=KEY2 pull=NONE
[I][sticks3.phase0] [Phase0:C] KEY1(GPIO11)=? KEY2(GPIO12)=? same_level=...
```

`same_level` 只表示两个 GPIO 当前电平相同，不代表已经可靠识别“双键按下”。必须根据已确定的按下电平进行人工判断。

请记录：

```text
空闲电平：KEY1=__，KEY2=__
按下电平：KEY1=__，KEY2=__
有效电平：active-high / active-low
GPIO11 物理按键：A / B / 未确定
GPIO12 物理按键：A / B / 未确定
最终 pull 模式：NONE / UP / DOWN
双键 20 次成功数：__/20
```

## Test D：关闭功放并接收 RAW 红外

目标：确认扬声器功放关闭后，StickS3 内置 IR RX 能通过 ESP32-S3 RMT DMA 输出解调后的 RAW mark/space 时序。

安全要求：

- 保持 USB-C 供电。
- 遥控器与 StickS3 红外接收端尽量正对。
- 距离至少 30 cm；不要紧贴接收器。
- Test D 运行时不要从 Grove 或 EXT_5V 输出端反向供电。

检查项目：

- [ ] 编译并刷入 `test_d.yaml`。
- [ ] 日志显示 M5PM1 初始化成功。
- [ ] 日志显示 `M5PM1 GPIO3 amplifier-low result=YES`。
- [ ] 日志显示 `Speaker amplifier disabled and EXT_5V enabled`。
- [ ] 日志明确提示 RX 是 demodulated，未声称能够测量载波。
- [ ] 完全不操作遥控器时观察 5 分钟，没有连续噪声或反复出现 RAW 帧。
- [ ] 用一个简单、已知可用的红外遥控器按一次，出现一条完整 RAW 输出。
- [ ] 用简单遥控器重复 10 次，比较脉冲数量和整体形状是否基本一致。
- [ ] 用 PAC13AS 遥控器按一次，确认日志没有明显截断。
- [ ] PAC13AS 重复 10 次，获得 10 次一致的完整捕获。
- [ ] 接收过程中 Wi-Fi、API、串口日志和 OTA 仍保持响应。
- [ ] 保存完整串口日志；若疑似截断，先保存原始日志，不要立即反复修改缓冲参数。

预期关键日志：

```text
[I][sticks3.phase0] [Phase0:D] M5PM1 GPIO3 amplifier-low result=YES
[I][sticks3.phase0] [Phase0:D] Speaker amplifier disabled and EXT_5V enabled
[W][sticks3.phase0] [Phase0:D] RX is demodulated; carrier measurement is not claimed
[I][remote.raw] Received Raw: 3400, -1750, 450, -420, ...
```

正数是 mark，负数是 space，单位为微秒。本测试不测量载波频率。除非后续出现明确的官方硬件证据，否则结论应记录为：

```text
载波频率不可测量；V1 默认使用可配置的 38 kHz。
```

## 最终结果记录

```text
Test A：通过 / 失败 / 未完成
Test B：通过 / 失败 / 未完成
Test C：通过 / 失败 / 未完成
Test D：通过 / 失败 / 未完成

ESPHome 启动稳定性：
M5Unified/M5PM1 共存情况：
LCD 情况：
按键结论：
RMT RAW 捕获情况：
PAC13AS 10 次完整捕获数：__/10
载波结论：不可测量 / 有新证据待复核
异常日志或照片位置：
```

## 决策规则

| 实测结果 | 决策 |
|---|---|
| A、B、C、D 的核心检查全部通过 | ESPHome + 外部组件架构可行 |
| A 通过，ESPHome 原生硬件组件不足，但 M5Unified 适配层稳定 | 使用 ESPHome + 薄 M5Unified/M5PM1 外部组件的混合方案 |
| M5Unified LCD 失败，但 ESPHome 网络仍稳定 | 先单独测试 ESPHome 原生 ST7789，再做决定 |
| 初始化 M5 后 Wi-Fi/API/OTA 变得不稳定 | 检查一次资源所有权；若为结构性冲突，转纯 PlatformIO |
| M5Unified/M5PM1 无法稳定共存 | 转纯 PlatformIO |
| RMT 截断，但使用官方支持的 DMA/缓冲设置后稳定 | 重新完成 PAC13AS 10 次测试，再决定是否保留 ESPHome |
| 功放关闭、距离正确后 RMT 仍持续噪声、截断或不可靠 | 转纯 PlatformIO |
| 无法测量载波频率 | 这是预期结果；保留可配置的 38 kHz 默认值 |

完成测试后保存完整日志、LCD 照片和按键记录。要恢复产品功能，请回到仓库
根目录并重新刷入 `irvault.yaml`。
