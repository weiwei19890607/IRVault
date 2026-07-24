# IRVault 真机测试清单

此清单用于正式固件，不适用于 `tools/hardware-validation` 中的硬件探针。

## 1. 编译与 USB 刷写

```bash
source .venv/bin/activate
esphome config irvault.yaml
esphome compile irvault.yaml
ls /dev/cu.usb*
esphome upload irvault.yaml --device /dev/cu.usbmodemXXXX
esphome logs irvault.yaml --device /dev/cu.usbmodemXXXX
```

启动检查：

- [ ] ESP32-S3、8 MB Flash、8 MB Octal PSRAM 正常；
- [ ] M5Unified/M5PM1 和 LCD 初始化成功；
- [ ] 日志出现原生 RMT RX GPIO42、TX GPIO46；
- [ ] 扬声器功放保持关闭，EXT_5V 已开启；
- [ ] Cool、Heat、Off 三个槽位均可读取；
- [ ] Wi-Fi 与 ESPHome API 正常；
- [ ] 屏幕右上角显示电量，低电量时变为红色；
- [ ] 无循环重启、brownout 或明显屏幕闪烁。

关键日志示例：

```text
[Storage] Variable RAW records ready max=2048 pulses ...
[IR] Native RMT RX GPIO42 and TX GPIO46 initialized ...
[APP] Long-frame HOME ready
```

## 2. 本机 UI

- [ ] Home 页面按 B 依次切换 Cool、Heat、Off；
- [ ] 按 A 打开当前槽位的 `Send / Learn / Cancel`；
- [ ] 菜单中 B 切换，A 确认；
- [ ] 15 秒无操作后，操作菜单自动返回 Home；
- [ ] 所有页面布局完整，倒计时只局部更新，不发生整屏频闪。

## 3. 学习三菱电机状态

准备遥控器时，应先设置完整目标状态：

- Cool：制冷、温度、风速与风向；
- Heat：制热、温度、风速与风向；
- Off：关机状态。

逐槽执行：

1. 进入 `Learn`；
2. 将遥控器发射端对准 StickS3 IR RX，先使用约 30 cm–1.5 m 距离；
3. 在 30 秒内清晰短按目标命令一次；
4. 学习成功后确认选项顺序为 `Save / Retry / Cancel`；
5. 选择 `Save`；
6. 断电重启后确认槽位仍可发送。

通过要求：

- [ ] 带噪声或残缺候选不会显示成功；
- [ ] 只接受签名 `23 CB 26 01 01` 且 18-byte checksum 正确的状态；
- [ ] 一个有效物理捕获即可通过，不要求第二次 match；
- [ ] 保存前会把有效状态重编码为 584 段标准双帧；
- [ ] 日志出现 18-byte state 与 V2 保存成功；
- [ ] 保存失败时旧资料仍然有效。

预期日志形式：

```text
[IR] Learning started
[IR] Accepted checksum-valid Mitsubishi Electric ...
[IR] Confirmed Mitsubishi Electric ... pulses=584 ...
[APP] Learning capture complete pulses=584
[Storage] Slot N saved V2 generation=... pulses=584 ...
```

若 30 秒内只有无效候选，屏幕应显示 `Invalid Signal`；完全无信号时显示
`No Signal`。不能退回到未校验的通用 RAW 保存。

## 4. 本机发送

对每个槽位：

1. 将 GPIO46 一端的 IR TX 对准空调接收窗，先使用约 0.5–2 m 距离；
2. 选择 `Send`；
3. 空调只应响应一次；
4. `Sent` 显示约 1.5 秒后应自动返回 Home；
5. 连续测试至少 5 次；
6. 断电重启后再次逐槽测试。

标准三菱资料虽然以 584 段双帧保存，但发送逻辑验证两帧一致、签名和
checksum 正确后只发送第一帧 292 段。这样可以避免已验证的室内机连续鸣叫
两次。预期日志：

```text
[APP] Sending slot N pulses=584
[IR] Replay queued stored_pulses=584 tx_pulses=292 symbols=146 carrier=38000Hz mode=Mitsubishi single-frame
[IR] Replay complete pulses=292 symbols=146 carrier=38000Hz
[APP] Sent status timeout; returning Home
```

`Replay complete` 只证明 RMT 发射结束；最终是否通过仍以空调真实响应为准。

## 5. Home Assistant

- [ ] ESPHome 集成中存在 IRVault 设备；
- [ ] Cool、Heat、Off 三个 button 实体均启用；
- [ ] 三个实体分别发送正确槽位；
- [ ] 本机正在学习或发送时，远程请求被安全拒绝；
- [ ] 连续远程发送不会重启、串槽或破坏本机 UI；
- [ ] 如需 Apple“家庭”，完成 HomeKit Bridge 配对与三个磁贴测试。

## 6. 长时间与恢复测试

- [ ] 连续在线 30 分钟，Wi-Fi、API、日志和 UI 正常；
- [ ] 完整断电重启 10 次，每次均可进入 Home；
- [ ] 重新学习同一槽并在保存前取消，旧资料不变；
- [ ] 保存新资料后 generation 增加；
- [ ] OTA 后资料仍存在；
- [ ] USB 重新刷写且未擦除 NVS 时，资料仍存在。

## 报错时保留的信息

- StickS3、空调和遥控器完整型号；
- 目标模式、温度、风速和风向；
- 从 `Learning started` 到结果页面的完整日志；
- 拒绝统计中的 input、cleaned、merged、headers、signature 和 checksum；
- 发送时 stored/tx pulse、symbol、载波、距离和方向；
- 是否听到一次或多次空调蜂鸣，以及空调实际执行结果。
