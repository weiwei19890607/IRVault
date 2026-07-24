# IRVault Phase 1-4 + 长帧存储升级

> 归档说明：这是开发阶段总结，其中的目录、命令和部分阶段描述已经过时。
> 当前安装与使用方法见仓库根目录 `README.md`。

This directory now contains the Phase 1 product skeleton plus the approved
Phase 2/3 receive and learning implementation and Phase 4 local RAW replay.
It exposes three momentary Home Assistant command buttons for the saved slots.

## Architecture

- ESPHome owns Wi-Fi, encrypted Native API, OTA, logging, safe mode and mDNS.
- `AppController` owns application state and routes button events.
- `StickS3Hardware` initializes the verified StickS3/M5Unified/M5PM1 hardware.
- `ButtonManager` debounces GPIO11/GPIO12 and emits semantic events without
  blocking.
- `ScreenManager` renders the 135x240 home screen through M5GFX.
- The top-right battery percentage is read from M5PM1 through M5Unified and
  updated independently every 10 seconds to avoid full-screen flicker.
- `IRManager` uses native ESP-IDF RMT RX/TX on GPIO42/GPIO46; ESPHome
  `remote_receiver` is not used. Learning is deliberately limited to the
  StickS3-tested 144-bit Mitsubishi Electric A/C variant. A candidate must
  decode to the exact `23 CB 26 01 01` signature and pass the 18-byte checksum;
  only then is it rebuilt as two nominal-timing frames for storage. Unknown
  RAW, Samsung and NEC signals are rejected.
  RX/TX capacity is 2048 RAW durations / 1024 RMT symbols. The large symbol
  buffers use internal DMA-capable RAM; five 8.2 KB working profiles and the
  storage serialization buffer use PSRAM. Replay applies the profile's
  configured carrier field (38 kHz by default).
- `StorageManager` uses V2 variable-length NVS blobs and keeps two CRC-verified
  generations per slot, so only the actual durations are written and an
  interrupted replacement cannot invalidate the previous profile. The default
  8 MB partition table provides a 0x60000-byte NVS partition.
- The firmware contains checksum-validated Cool, Heat and Off factory states
  extracted from the verified device. On a new device or after NVS erasure,
  each empty slot is seeded as a canonical 584-pulse profile. Existing learned
  or migrated records always take priority and are never overwritten at boot.
- Existing fixed-size V1 records (maximum 512 durations) are detected and
  copied to V2 on first boot. V1 records are deliberately retained so a
  firmware rollback does not destroy them.

## Verified GPIO

| Function | GPIO |
|---|---:|
| LCD MOSI / SCK / DC / CS / RST / BL | 39 / 40 / 45 / 41 / 21 / 38 |
| Button A / KEY1 | 11 |
| Button B / KEY2 | 12 |
| IR TX | 46 |
| IR RX | 42 |
| M5PM1 I2C SCL / SDA | 48 / 47 |

Buttons are active-low. Phase 0 verified independent and simultaneous reads.
IR RX requires RMT, EXT_5V power, and speaker-amplifier shutdown; these are
initialized by `StickS3Hardware` and `IRManager`.

## Build and flash on macOS

Reuse the Phase 0 virtual environment:

```bash
cd /absolute/path/to/IRVault
cp secrets.example.yaml secrets.yaml
nano secrets.yaml
esphome config irvault.yaml
esphome compile irvault.yaml
esphome upload irvault.yaml --device /dev/cu.usbmodemXXXX
esphome logs irvault.yaml --device /dev/cu.usbmodemXXXX
```

## Home Assistant discovery

The node advertises itself through ESPHome mDNS and supports the encrypted
Native API. Add it with Home Assistant's ESPHome integration using `irvault`
or the assigned IP. It exposes:

- `Cool` (`mdi:snowflake`) → slot 1
- `Heat` (`mdi:fire`) → slot 2
- `Off` (`mdi:power`) → slot 3

All three entities call the same validated V2 load and asynchronous RMT path
as the local UI. API commands are rejected while learning, waiting at
Save/Retry/Cancel, or already transmitting, so an unsaved capture and the
shared TX buffer cannot be overwritten.

See `HA_HOMEKIT_SETUP_ZH.md` for Home Assistant discovery, entity testing and
Apple HomeKit Bridge pairing.

## Current local workflow

- On Home, B selects a slot and A opens Send/Learn/Cancel.
- Learn listens for 30 seconds and scans each RMT candidate for a complete
  144-bit Mitsubishi Electric frame.
- Pulses of 220 microseconds or less are only removed when they are an edge
  glitch or conservatively merged between two equal-polarity neighbours.
- A frame is accepted only when its timing classes, exact signature and
  checksum all pass. One checksum-valid frame is sufficient; a second physical
  button press is not required.
- If receiver interference damages only the leader, the learner may recover
  from a complete 144-bit data section without a valid header. This path still
  requires all 144 timing decisions, the exact 40-bit signature, checksum and
  footer mark; it is not a generic RAW fallback.
- The received timing is not saved verbatim. The decoded 18-byte state is
  encoded as two canonical frames (584 RAW durations) before Save is offered.
- After capture, confirmation order is Save/Retry/Cancel; B selects and A
  confirms.
- Send loads and validates the selected slot, displays Sending, and replays it
  asynchronously on GPIO46 using the configured carrier. A stored canonical
  Mitsubishi profile retains both validated frames, but TX sends only its first
  292-pulse frame because the verified indoor unit acknowledges two frames as
  two separate commands. Other RAW profiles are transmitted unchanged. RMT
  completion, not queue submission, changes the screen to `Sent`; this prevents
  a long A/C frame from blocking the ESPHome loop. A successful `Sent` status
  returns to Home automatically after 1.5 seconds; errors still wait for A.
- Empty, invalid, or failed transmissions show a recoverable result screen.

## Mitsubishi Electric air-conditioner scope

This build is intentionally Mitsubishi Electric A/C-specific. It decodes the
common 144-bit / 18-byte state used by the user's verified remote and accepts
only the `23 CB 26 01 01` variant. The checksum byte must equal the low eight
bits of the sum of the preceding 17 bytes. Other Mitsubishi variants, 112-bit
or 136-bit frames, and arbitrary RAW signals are deliberately rejected rather
than risking a false successful learning result.

The RMT idle threshold is 30 ms, so the approximately 15.5 ms inter-frame gap
used by the common 144-bit `MITSUBISHI_AC` format remains inside one capture.
The learner can recover one valid frame from a longer noisy capture. It then
stores two clean frames with nominal 3400/1750 microsecond header timings,
450-microsecond marks, 420/1300-microsecond bit spaces and a 15.5 ms repeat
gap. Carrier frequency is still not measured; the configured field remains
38 kHz.

See `PHASE4_TEST_CHECKLIST_ZH.md` for the existing Phase 4 tests and
`LONG_FRAME_TEST_CHECKLIST_ZH.md` for the Mitsubishi long-frame acceptance
test.
