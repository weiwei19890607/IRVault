# StickS3 IR TX 长发射诊断

This is a visibility diagnostic derived from M5Stack's official StickS3 RMT
transmitter configuration. It emits a 500 ms, 38 kHz, 33% duty-cycle burst
from GPIO46 every two seconds.

It is not an IR protocol command and is only intended to make the IR LED
easier to observe with a camera or external IR detector.

```sh
arduino-cli compile --fqbn m5stack:esp32:m5stack_sticks3 ir_tx_long_burst
arduino-cli upload --fqbn m5stack:esp32:m5stack_sticks3 \
  --port /dev/cu.usbmodemXXXX ir_tx_long_burst
arduino-cli monitor --port /dev/cu.usbmodemXXXX \
  --config baudrate=115200
```

肉眼通常看不到红外光，应使用对该波段敏感的摄像头或红外检测器。此草图会
覆盖正式固件；测试后请重新刷入根目录的 `irvault.yaml`。
