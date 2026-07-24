# IRVault 接入 Home Assistant 与 Apple“家庭”

## 固件提供的实体

IRVault 通过 ESPHome Native API 暴露三个瞬时按钮：

| Home Assistant 实体 | 槽位 | 功能 |
|---|---:|---|
| `Cool` | 1 | 读取并发送 Cool 槽 |
| `Heat` | 2 | 读取并发送 Heat 槽 |
| `Off` | 3 | 读取并发送 Off 槽 |

预计 entity ID 为 `button.irvault_cool`、`button.irvault_heat` 和
`button.irvault_off`。Home Assistant 可能因为同名实体自动调整 ID，应以
设备页面显示为准。

这三个按钮复用本机 UI 的 NVS 校验和异步 RMT 发送路径。正在学习、停留在
Save/Retry/Cancel 页面或已经发送其它命令时，远程按钮请求会被拒绝，避免
覆盖未保存的数据或重复占用 TX 缓冲。

## 1. 确认网络与 API

StickS3 只连接 2.4 GHz Wi-Fi。通过 USB 查看日志：

```bash
esphome logs irvault.yaml --device /dev/cu.usbmodemXXXX
```

确认出现 Wi-Fi 已连接和 IP 地址。Home Assistant 主机必须能访问该 IP；若
mDNS 正常，也可以使用 `irvault.local`。

## 2. 添加 ESPHome 集成

在 Home Assistant 中进入 `设置 → 设备与服务`。

如果自动发现 IRVault，点击“配置”。如果没有：

1. 点击“添加集成”；
2. 搜索并选择 `ESPHome`；
3. Host 填 `irvault.local` 或设备日志中的 IP；
4. Port 使用 `6053`；
5. Encryption key 填根目录 `secrets.yaml` 中的
   `api_encryption_key`，不是 OTA password。

完成后进入 IRVault 设备页，应看到 Cool、Heat、Off 三个 button 实体。如果
设备存在但没有实体：

1. 确认刷入的是根目录 `irvault.yaml`，不是 `tools/hardware-validation`
   里的测试固件；
2. 在实体列表中关闭“仅显示已启用实体”，搜索 `irvault`；
3. 确认实体没有被禁用；
4. 重新加载 ESPHome 集成，必要时删除旧设备后用 IP 手动添加；
5. 保存从设备启动到 API 连接完成的完整日志。

## 3. 先在 Home Assistant 验证

分别点击三个实体。预期日志类似：

```text
[API] Cool button requested slot 1
[APP] Sending slot 1 pulses=...
[IR] Replay queued stored_pulses=... tx_pulses=... symbols=... carrier=38000Hz mode=...
[IR] Replay complete pulses=... symbols=... carrier=38000Hz ...
```

Heat 和 Off 只会改变槽位编号与名称。虽然固件为全新 NVS 提供默认资料，仍应
先在真实空调前逐个验证，或先用自己的遥控器重新学习。

## 4. 通过 HomeKit Bridge 加入 Apple“家庭”

StickS3 本身不是 HomeKit 或 Matter 配件，因此不要在 iPhone 中直接搜索
StickS3。

在 Home Assistant 中：

1. `设置 → 设备与服务 → 添加集成`；
2. 选择 `HomeKit Bridge`；
3. 选择“包含”模式，只勾选 IRVault 的 Cool、Heat、Off 三个 button；
4. 完成配置，取得 Home Assistant 显示的 HomeKit QR Code 或 PIN。

然后在 iPhone 上：

1. 打开“家庭”App；
2. 选择 `＋ → 添加配件`；
3. 扫描 Home Assistant 的 QR Code，或从“更多选项”选择
   `Home Assistant Bridge`；
4. 如系统提示未认证配件，确认继续；
5. 分配房间并完成配对。

Home Assistant 的 button 在 HomeKit 中可能呈现为开关式磁贴，但每次点击只
触发一次命令，不代表空调的真实开关、温度或工作模式状态。IRVault 当前不从
空调读取状态，也不提供恒温器实体。

配对期间 iPhone 与 Home Assistant 必须位于可互访的局域网，网络需要允许
mDNS（UDP 5353）。HomeKit Bridge 的 TCP 端口以 Home Assistant 实际配置
为准。
