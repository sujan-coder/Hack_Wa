# HackWa – ESP32-C6 Wearable Watch

A DIY smartwatch and hacking multi-tool built on the **Seeed XIAO ESP32-C6** with an **SSD1306 128×64 OLED** display and four physical buttons. It pairs with a custom Android companion app over BLE and doubles as a wireless keyboard password manager.

---

## What Is This?

HackWa started as a simple idea: what if a smartwatch could also be a security tool? The result is a wearable that runs in two distinct modes:

- **Watch Mode** – a fully functional digital watch with BLE phone notifications, stopwatch, countdown timer, and a "Find My Phone" feature.
- **HID Mode** – the watch re-advertises itself as a Bluetooth keyboard. Pick a saved password from the OLED screen, press a button, and it types the password into whatever computer or phone is connected. No more typing or pasting from a password manager on machines.

On top of that, the watch has a **Power Tools** menu with a BLE scanner (see nearby Bluetooth devices and their signal strength) and a BLE advertising jammer for pen-testing scenarios.

---

## Features

### Everyday Watch Stuff
- **Digital clock face** with 7-segment style rendering on the OLED
- **Time sync** – automatically synced from your phone via BLE when the companion app connects
- **Timezone support** – 11 presets (UTC, US regions, UK, Europe, India, China, Japan, Australia) configurable from the app
- **Phone notifications** – incoming alerts from your phone appear on the watch in real time; scroll through them, or clear them all
- **Stopwatch** – start/stop/reset with centisecond precision
- **Countdown timer** – adjustable from 1 to 90 minutes in 1-minute steps
- **Find My Phone** – press a button on the watch and your phone plays an alarm at max volume, even if it's on silent
- **Find My Watch** – tap a button in the app and the watch's OLED blinks "FIND ME!" so you can spot it

### Password Manager (HID Mode)
- Stores up to **10 passwords** in on-chip NVS flash (survives reboots and power loss)
- Switch to HID mode and the watch appears as a standard **Bluetooth keyboard** to any computer or phone
- Scroll through your saved passwords on the OLED, select one, and the watch **types it out keystroke by keystroke** then hits Enter
- Works with any OS – Windows, macOS, Linux, Android, iOS – anything that supports a BLE keyboard
### Power Tools
- **BLE Scanner** – passive scan that lists up to 20 nearby Bluetooth devices with their name, MAC address, and RSSI signal strength
- **BLE Jammer** – floods the 2.4 GHz BLE advertising channels for pen-testing and security research (shows live packet count and elapsed time)
- **Always-On Display (AOD)** – keeps the clock visible without sleep timeout; configurable auto-off timer from 5 to 120 minutes

### Power Saving
Power consumption matters on a tiny device with no big battery. Here's what the firmware does to stretch battery life:

- **Auto-sleep** – the OLED turns off after 30 seconds of inactivity (configurable from 5 to 300 seconds via the companion app). The MCU drops to a slower polling rate (200 ms ticks instead of 50 ms) while the screen is off.
- **HID mode gets an even shorter timeout** – the screen caps at 10 seconds in HID mode because you only need to glance at it to pick a password.
- **Dirty-screen tracking** – the clock face only redraws once per second, and only when the time actually changes. No wasted I2C bus traffic pushing the same pixels.
- **Brightness control** – three levels (dim / medium / bright) adjustable from the watch or the app, persisted in NVS so it remembers your preference across reboots.
- **Active tool override** – sleep is intelligently disabled when you're in the middle of using the stopwatch, timer, BLE scanner, or jammer so it doesn't cut out on you, but resumes normal sleep behavior as soon as you leave those screens.

---

## Companion App (Android)

The companion app is built with **Jetpack Compose** and runs a **foreground service** to stay connected in the background. It handles everything the watch can't do on its own.

### What the app does
- **Scans and pairs** with the watch over BLE (saves the device so it auto-reconnects next time)
- **Syncs time and timezone** automatically on connection
- **Forwards phone notifications** to the watch in real time
- **Password manager UI** – add, edit, and delete passwords from a proper keyboard instead of fiddling with AT commands
- **Send custom notifications** to the watch (preset templates or custom title + body)
- **Settings panel** – adjust screen timeout, brightness, and timezone from a proper UI
- **Find My Watch** – triggers the OLED blink alert on the watch
- **Raw AT command terminal** – for debugging or power users who want to send commands directly
- **BLE log** – scrollable terminal-style log of every command sent and received

### App screens
1. **Scan** – shows nearby BLE devices with name, MAC, and signal strength
2. **Dashboard** – quick-action cards for Sync Time, Send Notification, Password Manager, Settings, and Find Watch
3. **Passwords** – manage the 10 password slots with show/hide toggle
4. **Notifications** – compose and send notifications with preset templates
5. **Settings** – timezone, screen timeout slider, brightness selector, raw command input

---

## Hardware

| Component | Part |
|-----------|------|
| MCU | Seeed XIAO ESP32-C6 (RISC-V, BLE 5.0, Wi-Fi 6) |
| Display | SSD1306 128×64 OLED (I2C) |
| Buttons | 4× tactile switches (UP, DOWN, SELECT, BACK) 1x slide switch (On/Off) |
| Battery | 150 mah |

### Pin Wiring

| Function | GPIO |
|----------|------|
| OLED SDA | GPIO22 (D4) |
| OLED SCL | GPIO23 (D5) |
| Button UP | GPIO0 (D0) |
| Button DOWN | GPIO1 (D1) |
| Button SELECT | GPIO2 (D2) |
| Button BACK | GPIO21 (D3) |

---

## Button Controls

### Watch Mode (Main Screen)

| Button | Short Press | Long Press |
|--------|-------------|------------|
| **UP** | Open Stopwatch | — |
| **DOWN** | Find My Phone (if BLE connected) | — |
| **SEL** | View notifications | Open Power Tools menu |
| **BACK** | Switch to HID Mode | — |

### Notification Screen

| Button | Action |
|--------|--------|
| **UP / DOWN** | Scroll through notifications |
| **SEL** | Clear all notifications |
| **BACK** | Return to clock |

### Stopwatch

| Button | Action |
|--------|--------|
| **SEL** | Start / Stop |
| **DOWN** | Reset (when stopped) |
| **BACK** | Return to clock |

### Power Tools Menu

| Button | Action |
|--------|--------|
| **UP / DOWN** | Navigate menu items |
| **SEL** | Open selected tool |
| **BACK** | Return to clock |

Tools available: Always-On Display, Countdown Timer, BLE Jammer, BLE Scanner.

### HID Mode

| Button | Action |
|--------|--------|
| **UP / DOWN** | Scroll password list |
| **SEL** | Type selected password + Enter |
| **BACK** | Switch back to Watch Mode |

---

## Managing Passwords

Passwords are stored in NVS flash (10 slots, labels up to 20 chars, passwords up to 32 chars). They persist across reboots.

### From the Companion App (recommended)

Open the app → Dashboard → **Passwords**. Add, view, or delete passwords from there. Changes are synced to the watch over BLE instantly.

### From Any BLE Terminal App

Connect to the watch with a BLE UART app (nRF Connect, Serial Bluetooth Terminal, LightBlue, etc.) and send AT commands to the NUS RX characteristic:

```
AT+PS=0|Gmail|mypassword123
AT+PS=1|GitHub|s3cur3p@ss!
AT+PS=2|WiFi Home|RouterPassword42
```

**Format:** `AT+PS=<slot>|<label>|<password>`

Delete a slot:
```
AT+PD=2
```

### AT Command Reference

| Command | What It Does |
|---------|-------------|
| `AT+DT=20260219153000` | Set date and time (YYYYMMDDHHmmss) |
| `AT+NT=Title\|Body text` | Send a notification to the watch |
| `AT+TZ=EST5EDT` | Set POSIX timezone string |
| `AT+PS=0\|Label\|Pass` | Store password in slot 0 |
| `AT+PD=0` | Delete password in slot 0 |
| `AT+FW` | Trigger Find My Watch alert |
| `AT+ST=30` | Set screen timeout (seconds) |
| `AT+BR=1` | Set brightness (0=dim, 1=med, 2=bright) |

---

## Building the Firmware

The firmware is built with **PlatformIO** using the **ESP-IDF** framework:

```bash
pio run                    # build
pio run -t upload          # flash to watch
pio device monitor         # serial monitor (115200 baud)
```

## Building the Companion App

Open `companion_app/` in Android Studio and build normally, or from the command line:

```bash
cd companion_app
./gradlew assembleDebug
```

The APK will be at `companion_app/app/build/outputs/apk/debug/`.

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Firmware | C (ESP-IDF + NimBLE) on FreeRTOS |
| Display driver | Custom SSD1306 I2C driver with drawing primitives, 7-segment digits, and text rendering |
| BLE stack | NimBLE with CTS, ANS, NUS, BAS, DIS services (watch mode) and HID service (keyboard mode) |
| Password storage | ESP32 NVS (Non-Volatile Storage) flash |
| Companion app | Kotlin + Jetpack Compose + BLE foreground service |

## Pin Wiring

| Component    | Pin       | GPIO |
|--------------|-----------|------|
| OLED SDA     | D4        | 22   |
| OLED SCL     | D5        | 23   |
| Button UP    | D0        | 0    |
| Button DOWN  | D1        | 1    |
| Button SEL   | D2        | 2    |
| Button BACK  | D3        | 21   |

---

## Build & Flash

```bash
# Build
~/.platformio/penv/bin/pio run

# Upload
~/.platformio/penv/bin/pio run --target upload

# Monitor serial
~/.platformio/penv/bin/pio device monitor
```

<img width="1006" height="2337" alt="homepage" src="https://github.com/user-attachments/assets/66dbbed9-4d53-4e3a-ba3f-9e84031fd014" />


