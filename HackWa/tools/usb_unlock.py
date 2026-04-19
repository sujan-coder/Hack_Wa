#!/usr/bin/env python3
"""
HackWa USB Password Typer
──────────────────────────
Listens on a serial port for commands from the HackWa watch
and types them into the active window using xdotool (Linux) or
pyautogui (Windows/macOS).

Commands from watch (via USB serial):
    HACKWA_PW:<password>   → type password with random per-key delays
    HACKWA_ENTER           → press Enter key

Usage:
    python usb_unlock.py                     # auto-detect port
    python usb_unlock.py /dev/ttyACM0        # explicit port
    python usb_unlock.py COM3                # Windows

Requires:
    pip install pyserial
    (Linux) xdotool   – sudo apt install xdotool
    (Win/Mac) pip install pyautogui
"""

import sys
import time
import random
import serial
import serial.tools.list_ports
import subprocess
import platform
import shutil

BAUD      = 115200
TAG_PW    = "HACKWA_PW:"
TAG_ENTER = "HACKWA_ENTER"

# ── Keyboard typing backends ────────────────────────────────────

def type_char_xdotool(ch):
    """Type a single character using xdotool."""
    subprocess.run(["xdotool", "type", "--clearmodifiers", ch],
                   check=True, capture_output=True)

def send_enter_xdotool():
    """Press Enter using xdotool."""
    subprocess.run(["xdotool", "key", "Return"],
                   check=True, capture_output=True)

def type_char_pyautogui(ch):
    """Type a single character using pyautogui."""
    import pyautogui
    pyautogui.press(ch) if len(ch) == 1 else pyautogui.typewrite(ch)

def send_enter_pyautogui():
    """Press Enter using pyautogui."""
    import pyautogui
    pyautogui.press("enter")

# Backend selection
_type_char  = None
_send_enter = None

def init_backend():
    """Pick the best available typing backend."""
    global _type_char, _send_enter
    if platform.system() == "Linux" and shutil.which("xdotool"):
        _type_char  = type_char_xdotool
        _send_enter = send_enter_xdotool
        return
    try:
        import pyautogui  # noqa: F401
        _type_char  = type_char_pyautogui
        _send_enter = send_enter_pyautogui
        return
    except ImportError:
        pass
    print("ERROR: No typing backend available.")
    print("  Linux  → sudo apt install xdotool")
    print("  Other  → pip install pyautogui")
    sys.exit(1)

# ── Teensy-style password typer ─────────────────────────────────

def type_password(password):
    """
    Type password with random 5-20 ms delay between characters,
    mimicking a Teensy HID keyboard to avoid bot-detection.
    """
    for ch in password:
        _type_char(ch)
        delay = random.uniform(0.005, 0.020)   # 5–20 ms
        time.sleep(delay)
    # 50 ms pause after full password (like Teensy reference)
    time.sleep(0.050)

# ── Serial port helpers ─────────────────────────────────────────

def find_port():
    """Auto-detect likely ESP32-C6 USB-Serial/JTAG port."""
    for p in serial.tools.list_ports.comports():
        desc = (p.description or "").lower()
        vid  = p.vid or 0
        # Espressif USB-Serial/JTAG VID=0x303a
        if vid == 0x303a or "usb-serial" in desc or "jtag" in desc:
            return p.device
    # Fallback: first ACM / ttyUSB / COM port
    for p in serial.tools.list_ports.comports():
        if "ACM" in (p.device or "") or "ttyUSB" in (p.device or ""):
            return p.device
    return None

# ── Main loop ───────────────────────────────────────────────────

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not port:
        print("No serial port found. Pass it as an argument:")
        print(f"  {sys.argv[0]} /dev/ttyACM0")
        sys.exit(1)

    init_backend()

    print(f"[HackWa] Listening on {port} @ {BAUD} baud")
    print("[HackWa] Switch to HID mode on watch (long-press BACK)")
    print("[HackWa] SELECT = type password  |  UP (long) = Enter")
    print("[HackWa] Ctrl+C to quit\n")

    while True:
        try:
            with serial.Serial(port, BAUD, timeout=1) as ser:
                while True:
                    line = ser.readline()
                    if not line:
                        continue
                    try:
                        text = line.decode("utf-8", errors="replace").strip()
                    except Exception:
                        continue

                    if text.startswith(TAG_PW):
                        pw = text[len(TAG_PW):]
                        print(f"[HackWa] Typing password ({len(pw)} chars)...")
                        time.sleep(0.3)          # focus settle delay
                        type_password(pw)
                        print("[HackWa] Password typed ✓")

                    elif text.startswith(TAG_ENTER):
                        print("[HackWa] Pressing Enter...")
                        _send_enter()
                        print("[HackWa] Enter ✓")

        except serial.SerialException as e:
            print(f"[HackWa] Serial error: {e}  — retrying in 2s...")
            time.sleep(2)
        except KeyboardInterrupt:
            print("\n[HackWa] Bye.")
            break

if __name__ == "__main__":
    main()
