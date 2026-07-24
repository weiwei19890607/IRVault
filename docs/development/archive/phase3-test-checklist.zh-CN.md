# IRVault Phase 3 真机测试清单

> 归档说明：此清单只记录早期接收/存储阶段，不能作为当前固件验收标准。
> 当前清单见 `docs/device-test.zh-CN.md`。

本阶段包含原生 RMT RAW 接收、20 秒学习会话、确认界面和双副本持久化。
不包含红外发送、回放或 Home Assistant 命令实体。

## 刷写

```bash
cd /absolute/path/to/IRVault
esphome compile irvault.yaml
esphome upload irvault.yaml --device /dev/cu.usbmodemXXXX
esphome logs irvault.yaml --device /dev/cu.usbmodemXXXX
```

## 启动

- 日志出现 `[APP] Phase 3 HOME ready`。
- 日志出现原生 RMT、功放关闭和 EXT_5V 启用信息。
- Cool、Heat、Off 在首次运行时均显示 Empty。
- Wi-Fi、API、OTA仍正常。

## 学习并保存

对每个槽位分别执行：

1. Home 中用 B 选择槽位，A确认。
2. 用 B 选择 Learn，A确认。
3. 屏幕显示20秒倒计时；保持至少30 cm距离，按遥控器一次。
4. 短噪声必须被忽略；有效主帧应进入 Signal OK。
5. 核对屏幕 pulse 数与日志一致。
6. 用 B选择 Save，A确认。
7. 必须显示 Saved / Profile verified。
8. B返回后，对应槽位必须显示 Learned。

## Retry、Cancel和超时

- Signal OK页面选择 Retry，应重新开始完整20秒学习会话。
- Signal OK页面选择 Cancel，应返回Home且不改变旧槽位。
- Learning页面按B，应立即取消并返回Home。
- 不按遥控器等待20秒，应显示 No Signal；B返回Home。
- Action菜单选择Cancel，应直接返回Home。

## 原子存储

- 保存后断电重启，槽位仍显示Learned。
- 对同一槽位重新学习，在Save之前取消，旧Learned状态必须保留。
- 重新学习并Save后，日志中的generation应递增。
- 保存失败时应显示Save Error / Old profile kept，旧配置不得丢失。

## 限制

- 学到的是解调后的RAW mark/space，不包含协议语义。
- 载波频率不能测量，配置中固定默认38 kHz。
- 最大保存512个pulse durations。
- 本阶段不能发送；Send仍显示Phase 4占位提示。

完成后请提供启动日志、一次成功学习与保存日志，以及断电重启后的槽位状态。
