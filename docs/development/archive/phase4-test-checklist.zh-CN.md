# IRVault Phase 4 真机测试清单

> 归档说明：此清单包含已经废弃的通用 Samsung/NEC 验证流程。当前固件只
> 接受指定三菱电机 144-bit 帧；请使用 `docs/device-test.zh-CN.md`。

本阶段加入本机 RAW 红外重放。仍不包含 Home Assistant 红外命令实体。

## 刷写与日志

```bash
cd /absolute/path/to/IRVault
esphome upload irvault.yaml --device /dev/cu.usbmodemXXXX
esphome logs irvault.yaml --device /dev/cu.usbmodemXXXX
```

本项目的真机验证统一使用 USB 刷写，不使用 IP/OTA 刷写。

## 启动检查

- 日志出现 `[APP] Long-frame HOME ready`。
- 日志出现 `[IR] Native RMT RX GPIO42 and TX GPIO46 initialized`。
- Phase 3 已保存的槽位在重启后仍显示 `Learned`。
- Wi-Fi、API 与 OTA 保持正常。
- 所有页面右上角均显示电量百分比；低于或等于 15% 时显示为红色。
- 电量区域约每 10 秒更新，不能引起整屏刷新或明显频闪。

## 学习确认页面微调

1. 对任一槽位开始 Learn；在 30 秒内将遥控器按键清晰地短按一次。
2. 第一份结构合格的信号应立即进入保存页面，不再要求第二次 timing match。
3. Samsung/NEC 的标准 32 位帧仍应为 67 个 RAW 脉冲；类似 Samsung
   头部但只有 63 个脉冲的残缺帧必须拒绝。
4. 收到合格信号后，三个选项顺序必须为：
   `Save`、`Retry`、`Cancel`。
5. 默认高亮必须是 `Save`。
6. 直接按 A 应成功保存；B 应按上述顺序循环选择。
7. Learn 倒计时只能每秒局部变化，不应整屏频闪。

Samsung 电源键成功确认的典型日志：

```text
[IR] Accepted valid Samsung code=E0E040BF sample=1/1
[IR] Confirmed Samsung code=E0E040BF samples=1 pulses=67 ...
[APP] Learning capture complete pulses=67
```

## 空槽保护

1. 选择一个 `Empty` 槽位。
2. 进入 `Send`。
3. 屏幕应显示 `Empty Slot / Learn this slot first`。
4. 日志应出现 `[APP] Cannot send empty slot N`。
5. 按 A 可以正常返回 Home，无重启。

## 单槽重放

对一个容易观察结果的电视或投影仪按键执行：

1. 用原遥控器学习并保存到任一槽位。
2. 将 StickS3 红外发射端对准设备；建议先测试约 0.5–2 米。
3. Home 用 B 选择该槽，A 进入菜单。
4. 菜单默认高亮 `Send`；需要时用 B 选择，按 A 确认。
5. 屏幕应短暂显示 `Sending...`，随后显示
   `Sent / RAW replay complete`。
6. 无需按键，`Sent` 页面应在约 1.5 秒后自动返回 Home；发送错误页面仍应
   保留，等待按 A 返回。
7. 被控设备应执行与原遥控器相同的动作。
8. 日志应出现：

```text
[APP] Sending slot N pulses=...
[IR] Replay queued pulses=... symbols=... carrier=38000Hz
[IR] Replay complete pulses=... symbols=... carrier=38000Hz
[APP] Sent status timeout; returning Home
```

重复发送至少 10 次，确认每次按 A 只发送一次，并且不会重启、卡死或
影响 Wi-Fi/API。

## Cool、Heat、Off 三槽

- 分别学习三个可区分的遥控命令并保存。
- 逐槽 Send，确认没有串槽。
- 断电重启后再次逐槽 Send，确认存储和重放仍正确。
- 如暂时没有空调遥控器，可以先用电视/投影仪的三个不同按键完成架构验证；
  空调完整状态帧仍需之后用真实空调遥控器单独验证。

## 失败与恢复

- 发送后无设备响应，但日志显示 Replay complete：记录距离、方向、槽位、
  pulse 数和原设备型号。这表示 RMT 已完成，不等同于目标设备已接受信号。
- 出现 `Send Error` 时，请保留从 `[APP] Sending` 到 `[IR] Replay failed`
  的完整日志。
- 任意错误页面按 A 都应返回 Home，不需要重启。

## Phase 4 通过条件

- Save/Retry/Cancel 新顺序正确。
- Empty 槽不会发送。
- 至少一个已学习槽能够可靠控制真实设备。
- 三槽选择不会串槽。
- 连续发送和重启后发送均稳定。
- Wi-Fi、API 与 OTA 没有回归。

满足以上条件后停止 Phase 4，并提供一次成功发送日志及真实设备响应结果，
再决定是否进入 Phase 5。
