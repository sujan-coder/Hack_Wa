# HackWa – ESP32-C6 Wearable Watch
A BLE wearable watch with **HackWa app integration** and **HID password manager**, built on the Seeed XIAO ESP32-C6 with an SSD1306 128×64 OLED.

## Button Controls

### Watch Mode

| Button  | Short Press              | Long Press (2 s) |
|---------|--------------------------|-------------------|
| **UP**  | Open Stopwatch           | —                 |
| **DOWN**| Cycle brightness (3 lvl) | —                 |
| **SEL** | View notifications       | —                 |
| **BACK**| — (return from sub-screen)| **Switch to HID mode** |

### Watch → Notification Screen

| Button  | Action                   |
|---------|--------------------------|
| **UP**  | Previous notification    |
| **DOWN**| Next notification        |
| **SEL** | Clear all notifications  |
| **BACK**| Return to clock          |

### Watch → Stopwatch Screen

| Button  | Action                   |
|---------|--------------------------|
| **SEL** | Start / Stop             |
| **DOWN**| Reset (when stopped)     |
| **BACK**| Return to clock          |

### HID Mode

| Button  | Action                     | Long Press (2 s) |
|---------|----------------------------|-------------------|
| **UP**  | Scroll up in password list | —                 |
| **DOWN**| Scroll down                | —                 |
| **SEL** | Type selected password + Enter | —             |
| **BACK**| Return to list from "Done" | **Switch to Watch mode** |

---

## Adding Passwords

Passwords are stored in NVS flash (10 slots, labels up to 20 chars, passwords up to 32 chars).

### Method 1: BLE UART (any BLE terminal app)

Connect to "HackWa" (or "HackWa-KB" in HID mode) with a BLE UART app like **nRF Connect**, **Serial Bluetooth Terminal**, or **LightBlue**. Send AT-commands to the NUS RX characteristic:

```
AT+PS=0|Gmail|mypassword123
AT+PS=1|GitHub|s3cur3p@ss!
AT+PS=2|WiFi Home|RouterPassword42
AT+PS=3|Laptop|MyLaptopPIN
```

**Format:** `AT+PS=<slot>|<label>|<password>`

- **slot**: 0–9 (10 available slots)
- **label**: Display name (max 20 chars, shown on watch)
- **password**: The password to type (max 32 chars)

### Delete a password

```
AT+PD=2
```

Removes the password in slot 2.

### Method 2: HackWa binary protocol

The watch also accepts passwords via the HackWa app's NUS data channel.

### Other AT-commands

| Command                  | Description                          |
|--------------------------|--------------------------------------|
| `AT+DT=20260219153000`   | Set date/time (YYYYMMDDHHmmss)       |
| `AT+NT=Title\|Body text`  | Push a test notification             |
| `AT+TZ=EST5EDT`          | Set POSIX timezone string            |
| `AT+PS=0\|Label\|Pass`    | Store password in slot 0             |
| `AT+PD=0`                | Delete password in slot 0            |

---

## Chronos App Setup

1. Install the **[Chronos](https://chronos.ke/app?id=esp32)** app from Google Play
2. Open app → **Watches** tab → **Pair New Devices** → **Search**
3. Select "**Chronos**" from the scan results
4. Time syncs automatically; notifications forwarded from your phone

> **Important:** Make sure the watch is NOT paired in your phone's system Bluetooth settings. Only pair through the Chronos app.

---

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
