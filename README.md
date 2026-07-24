# IRVault

IRVault is a Mitsubishi Electric air-conditioner infrared learning and control
firmware for the M5Stack StickS3. ESPHome provides Wi-Fi, the encrypted Native
API, OTA updates, and Home Assistant integration. M5Unified and M5PM1 drive the
display, power management, and physical buttons, while the ESP32-S3 native RMT
peripheral handles infrared reception and transmission.

The current firmware has been validated on real hardware for the following
workflow:

- Select one of three slots: Cool, Heat, or Off.
- Learn and validate a Mitsubishi Electric 144-bit / 18-byte A/C state frame.
- Store the learned profile in NVS with CRC validation and dual-copy recovery.
- Transmit any slot from the local UI or Home Assistant.
- Expose the three commands to Apple Home through Home Assistant's HomeKit
  Bridge.

## Scope and limitations

This firmware is not a universal infrared learner. The current learner accepts
only checksum-valid Mitsubishi Electric 144-bit frames with the signature
`23 CB 26 01 01`. Samsung, NEC, other Mitsubishi frame variants, and
unrecognized generic RAW signals are rejected to prevent receiver noise from
being saved as a valid command.

The infrared receiver exposes demodulated mark/space timing and cannot measure
the original remote's carrier frequency. Each profile retains a configurable
carrier field, currently defaulting to 38 kHz. A slot can hold up to 2,048 RAW
durations. A valid Mitsubishi state is normalized into two stored frames
containing 584 durations, but transmission sends only the first 292-duration
frame. This prevents the verified indoor unit from responding twice to one
command.

The firmware contains Cool, Heat, and Off factory profiles captured from the
currently verified device. They seed only new or erased slots and never
overwrite existing learned profiles. These defaults may not be compatible with
other A/C models or personal settings, so learning profiles from the intended
remote is recommended.

## Hardware and software requirements

- M5Stack StickS3 with ESP32-S3-PICO-1
- 8 MB Flash and 8 MB Octal PSRAM
- macOS and a data-capable USB-C cable
- Python 3.11 or later
- ESPHome 2026.7.1, pinned in `requirements.txt`
- M5Unified 0.2.18, fetched during compilation
- A 2.4 GHz Wi-Fi network

Verified hardware mappings:

| Function | GPIO |
|---|---:|
| Button A / Confirm | 11 |
| Button B / Select | 12 |
| IR TX | 46 |
| IR RX | 42 |
| M5PM1 I2C SCL / SDA | 48 / 47 |

Both buttons are active-low. Before infrared reception starts, the firmware
disables the speaker amplifier and enables the EXT_5V power domain.

## Repository layout

```text
IRVault/
|-- irvault.yaml                 # Production ESPHome configuration
|-- secrets.example.yaml         # Publishable configuration template
|-- requirements.txt             # Verified ESPHome version
|-- components/irvault_app/      # External component and application code
|-- tests/                       # Hardware-independent protocol tests
|-- docs/                        # Usage, testing, and development documents
`-- tools/
    |-- hardware-validation/     # Standalone hardware feasibility tests
    `-- arduino/                 # Official TX and long-burst diagnostics
```

The files under `docs/development/archive/` preserve early requirements and
phase-specific validation history. They do not describe the current firmware
behavior.

## Install ESPHome

Run the following commands from the repository root:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
esphome version
```

The virtual environment and ESPHome's generated `.esphome` directory are
excluded from Git.

## Create `secrets.yaml`

ESPHome reads the Wi-Fi credentials and encryption keys from a local
`secrets.yaml` file. This file is not included in the repository and must be
created before validating or compiling the firmware.

1. Copy the provided template from the repository root:

   ```bash
   cp secrets.example.yaml secrets.yaml
   ```

2. Generate three independent values. Run each command separately and keep its
   output for the next step:

   ```bash
   # ESPHome Native API encryption key: exactly 32 random bytes in Base64
   openssl rand -base64 32

   # Strong OTA update password
   openssl rand -hex 24

   # Fallback access-point password
   openssl rand -base64 18
   ```

3. Open the new file:

   ```bash
   nano secrets.yaml
   ```

4. Replace every placeholder with the appropriate value:

   ```yaml
   wifi_ssid: "YOUR_2_4_GHZ_WIFI_NAME"
   wifi_password: "YOUR_WIFI_PASSWORD"
   api_encryption_key: "PASTE_THE_BASE64_32_BYTE_KEY_HERE"
   ota_password: "PASTE_THE_HEX_OTA_PASSWORD_HERE"
   fallback_password: "PASTE_THE_FALLBACK_PASSWORD_HERE"
   ```

   The API encryption key must be the unmodified output of
   `openssl rand -base64 32`. The fallback password must contain at least eight
   characters. Use different values for the API key, OTA password, and
   fallback password.

5. In `nano`, press `Control-O`, then Enter to save, and `Control-X` to exit.
   Restrict the file so only the current macOS user can read it:

   ```bash
   chmod 600 secrets.yaml
   ```

6. Confirm that the resulting configuration is valid:

   ```bash
   esphome config irvault.yaml
   ```

The repository-level `.gitignore` excludes every `secrets.yaml` file. Never
paste real credentials into issues, logs, screenshots, or committed files.

## Validate, compile, and flash over USB

First, find the current serial device. Its numeric suffix may change:

```bash
ls /dev/cu.usb*
```

Then run:

```bash
source .venv/bin/activate
esphome config irvault.yaml
esphome compile irvault.yaml
esphome upload irvault.yaml --device /dev/cu.usbmodemXXXX
esphome logs irvault.yaml --device /dev/cu.usbmodemXXXX
```

Replace `/dev/cu.usbmodemXXXX` with the actual port. USB is recommended for the
first installation and for recovery. After the device has joined the intended
network reliably, OTA upload and logging are also available:

```bash
esphome upload irvault.yaml --device irvault.local
esphome logs irvault.yaml --device irvault.local
```

If USB upload cannot connect, hold the StickS3 side button until the internal
green LED flashes and the board enters download mode, then retry. A short press
of this button may power the unit off or on; it should not be treated as a
normal application reset button.

## Local controls

The entire UI follows one interaction model:

- Button B changes the current selection.
- Button A confirms the selection.

To send a slot:

1. Press B on the Home screen to select Cool, Heat, or Off.
2. Press A to open the action menu.
3. Select `Send` and press A.
4. After a successful transmission, `Sent` appears for approximately
   1.5 seconds before the UI returns to Home automatically.

To learn a slot:

1. Select the target slot and open its action menu.
2. Select `Learn` with B and confirm with A.
3. Within 30 seconds, point the Mitsubishi Electric remote at the StickS3 IR
   receiver and clearly press the intended command once.
4. After a valid capture, choose `Save`, `Retry`, or `Cancel`.
5. Select `Save` and confirm. A profile is offered for saving only after its
   signature, bit timing, and checksum have all passed validation.

An A/C remote normally transmits its complete state. Before learning Cool or
Heat, configure the original remote with the desired operating mode,
temperature, fan speed, and vane position.

## Home Assistant and Apple Home

The ESPHome Native API exposes three momentary button entities:

- Cool
- Heat
- Off

The StickS3 is not itself a HomeKit or Matter accessory. To use these commands
in the Apple Home app, first add IRVault to Home Assistant and then expose the
three entities through Home Assistant's HomeKit Bridge. See the
[Home Assistant and Apple Home setup guide](docs/home-assistant-homekit.zh-CN.md)
for the complete procedure.

## Testing

The protocol decoder, factory profiles, and single-frame transmission
selection can be tested on macOS without hardware:

```bash
c++ -std=c++17 -Icomponents/irvault_app \
  tests/mitsubishi_codec_test.cpp \
  components/irvault_app/mitsubishi_codec.cpp \
  components/irvault_app/factory_defaults.cpp \
  -o /tmp/irvault_mitsubishi_test
/tmp/irvault_mitsubishi_test
```

See the [device test checklist](docs/device-test.zh-CN.md) for real-hardware
acceptance testing. The standalone tests originally used to validate ESPHome,
M5Unified/M5PM1, the LCD, buttons, and RMT are preserved under
[`tools/hardware-validation`](tools/hardware-validation/).

## License

IRVault is distributed under the [MIT License](LICENSE).
