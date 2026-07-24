# CODING_PROMPT.md

> 归档说明：这是项目启动时的原始需求提示，不是当前规格。后续实机验证将
> 产品收敛为三菱电机 144-bit 专用学习器，并增加了长帧存储、校验、默认
> 资料和单帧发送。当前行为以仓库根目录 `README.md` 和源码为准。

# IRVault -- Product Coding Prompt for Codex

## Mission

Build a maintainable embedded application for **M5Stack StickS3** that
acts as a **generic RAW infrared profile manager**, not a
Mitsubishi-specific controller.

The firmware must **never interpret IR protocol semantics**. It only
learns, validates, stores, organizes and replays complete RAW infrared
profiles.

Target hardware: - M5Stack StickS3 (ESP32-S3) - Built-in LCD (135x240) -
Built-in IR TX/RX (verify GPIO from official docs before coding) - Two
hardware buttons only - USB-C - Wi-Fi

Target ecosystem: - ESPHome (Version 1) - Home Assistant Native API -
OTA updates - macOS development - VS Code + Codex

------------------------------------------------------------------------

# Design Principles

1.  ESPHome provides infrastructure only:

    -   Wi-Fi
    -   OTA
    -   Home Assistant API
    -   Logging

2.  Business logic belongs in reusable C++ modules.

3.  Avoid large YAML lambdas.

4.  Use explicit state machines.

5.  Never hardcode Mitsubishi protocol knowledge.

6.  Design for future migration to pure PlatformIO.

------------------------------------------------------------------------

# Product Goal

The device behaves as a generic IR learner/player.

Version 1 supports exactly three slots:

-   Slot 1 (display name: Cool)
-   Slot 2 (display name: Heat)
-   Slot 3 (display name: Off)

Internally these are only slots.

The firmware must NOT assume "Cool" means cooling.

Display names are UI labels only.

------------------------------------------------------------------------

# User Workflow

## Learn

User selects slot.

↓

Device enters learning mode.

↓

User configures original remote exactly as desired.

Example:

Cool: - Cooling - 25°C - Auto fan

Heat: - Heating - 24°C - Auto fan

Off: - Power Off

↓

User presses original remote once.

↓

StickS3 captures complete RAW frame.

↓

User confirms.

↓

Saved into persistent storage.

------------------------------------------------------------------------

## Replay

Home Assistant

↓

Button Press

↓

Firmware loads RAW profile

↓

Replay exactly.

No protocol parsing.

------------------------------------------------------------------------

# Software Architecture

components/

-   IRManager
-   StorageManager
-   ButtonManager
-   ScreenManager
-   AppController

Responsibilities:

IRManager - learn() - send() - validate()

StorageManager - saveSlot() - loadSlot() - eraseSlot()

ButtonManager - SHORT_A - SHORT_B - LONG_A - LONG_B - BOTH_LONG

ScreenManager - renderHome() - renderMenu() - renderLearning() -
renderSending() - renderError()

AppController - state machine - event routing

------------------------------------------------------------------------

# State Machine

HOME

MENU

LEARN_CONFIRM

LEARNING

SAVE_CONFIRM

SENDING

ERROR

SETTINGS (reserved)

Only AppController changes states.

------------------------------------------------------------------------

# UI

Home Screen

IRVault

WiFi: OK/OFF

HA: Connected/Offline

Cool : Learned / Empty

Heat : Learned / Empty

Off : Learned / Empty

Bottom:

A Menu

B Send

------------------------------------------------------------------------

Menu

Cool

Heat

Off

Status

A -\> Next

B -\> Select

Both Long -\> Learn

15 second timeout returns Home.

------------------------------------------------------------------------

Learning

Learning COOL

Set remote first

Press remote once

20 second timeout.

------------------------------------------------------------------------

Signal Received

Signal OK

Pulses: XXX

A Retry

B Save

------------------------------------------------------------------------

Sending

Sending...

Cool

Done

------------------------------------------------------------------------

# Storage

Each slot stores:

-   valid
-   display name
-   carrier frequency
-   pulse count
-   raw timing array
-   CRC
-   version
-   created timestamp
-   last used timestamp

Carrier frequency limitation for Version 1:

-   Learning captures demodulated RAW mark/space timings only.
-   Carrier frequency is not claimed to be measured or automatically learned.
-   The stored carrier frequency defaults to 38 kHz.
-   Treat carrier frequency as a configurable profile parameter for future use.

Requirements

Atomic save.

Never overwrite valid data until new data verified.

OTA safe.

Power-loss tolerant.

------------------------------------------------------------------------

# Home Assistant

Expose:

Buttons

-   Cool

-   Heat

-   Off

-   Learn Cool

-   Learn Heat

-   Learn Off

-   Clear Cool

-   Clear Heat

-   Clear Off

Sensors

-   Cool Learned

-   Heat Learned

-   Off Learned

-   Last Command

-   Last Pulse Count

-   RSSI

-   Uptime

Version 1 should NOT expose a Climate entity.

------------------------------------------------------------------------

# Button Rules

Short A

Move selection

Short B

Execute selection

Both Long

Enter learning

Long A/B reserved

Debounce required.

No blocking loops.

------------------------------------------------------------------------

# Logging

Use prefixes:

\[APP\]

\[IR\]

[UI](#ui)

\[HA\]

[Storage](#storage)

\[WiFi\]

------------------------------------------------------------------------

# Error Handling

Learning timeout

Invalid RAW

Storage failure

Replay failure

WiFi lost

HA disconnected

Every error must recover without reboot.

------------------------------------------------------------------------

# Development Phases

## Phase 0

Before product implementation, run an incremental StickS3 hardware and
framework feasibility prototype. Verify ESPHome boot, Flash/PSRAM, networking,
API and OTA; M5Unified/M5PM1 coexistence; static LCD output; both application
buttons; speaker-amplifier disable; and demodulated RAW capture through ESP32
RMT. Do not implement menus, profiles, storage, replay, or Home Assistant IR
command entities. Stop and wait for real-device results.

## Phase 1

Verify official StickS3 documentation.

Confirm:

-   IR TX GPIO
-   IR RX GPIO
-   LCD
-   Buttons
-   Speaker requirements
-   ESPHome compatibility

Implement:

-   WiFi
-   OTA
-   API
-   Screen
-   Buttons
-   Home screen

Stop.

Wait for user test.

## Phase 2

IR receiver.

Output RAW to logs.

Verify PAC13AS reception.

Stop.

## Phase 3

Persistent storage.

Learning flow.

Confirmation UI.

Stop.

## Phase 4

Replay RAW.

Test Cool/Heat/Off.

Stop.

## Phase 5

Home Assistant integration.

OTA validation.

Stop.

## Phase 6

Polish.

Documentation.

Testing.

------------------------------------------------------------------------

# Coding Rules

-   No hardcoded passwords.
-   secrets.yaml only.
-   No guessed GPIO values.
-   No delay()-based state machines.
-   Modular C++.
-   Document every public class.
-   Prefer composition over globals.
-   Keep modules reusable.
-   Prepare for future PlatformIO migration.

------------------------------------------------------------------------

# First Task for Codex

Do NOT implement the entire project.

Only complete Phase 1.

Deliver:

1.  Architecture
2.  Folder layout
3.  GPIO verification from official documentation
4.  ESPHome configuration
5.  Screen demo
6.  Button demo
7.  OTA
8.  Home Assistant discovery
9.  Build instructions
10. Test checklist

Stop and wait for test feedback before Phase 2.
