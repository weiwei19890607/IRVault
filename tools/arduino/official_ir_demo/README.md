# M5StickS3 官方 IR TX 验证草图

Source: M5Stack StickS3 IR Infrared Transmit & Receive documentation,
Transmitter example.

Board:

```text
m5stack:esp32:m5stack_sticks3
```

在 `tools/arduino` 目录编译：

```sh
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 official_ir_demo
```

通过 USB 上传：

```sh
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 \
  --port /dev/cu.usbmodemXXXX official_ir_demo
```

Monitor:

```sh
arduino-cli monitor --port /dev/cu.usbmodemXXXX \
  --config baudrate=115200
```

此草图会覆盖 IRVault 正式固件；测试后请重新刷入根目录的
`irvault.yaml`。
