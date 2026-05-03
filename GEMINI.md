# PassKey — Hardware Password Manager Architecture

This project is a hardware-based password vault combining an Arduino Uno (simulated via Wokwi) and a Python-based PC client.

## System Architecture

The security model relies on a "Zero-Trust" relationship between the PC and the Hardware:
- **PC Client (`passkey.py`):** Handles all cryptographic operations (AES-256-GCM, PBKDF2). It never sends the master password to the Arduino.
- **Hardware (`passkey_device.ino`):** Acts as a physical gatekeeper. It requires a 4-digit PIN entered via a physical keypad to unlock the serial communication bridge. It stores the encrypted "vault" in its internal EEPROM.

## Directory Structure

- `passkey/`
  - `passkey.py`: PC client. Manages encryption, decryption, and serial communication.
  - `passkey_device.ino`: Upgraded firmware for Arduino Uno + OLED + Keypad + RGB LED.
  - `passkey_bridge.ino`: Simplified firmware for plain Arduino Uno (no hardware UI).
  - `diagram.json`: Wokwi simulation configuration for the upgraded hardware.
  - `libraries.txt`: List of required Arduino libraries for Wokwi.
- `README.md`: User-facing setup and usage instructions.

## Hardware Components (Wokwi)

| Component | Interface | Pin(s) |
|---|---|---|
| Arduino Uno | Controller | - |
| SSD1306 OLED | I2C | A4 (SDA), A5 (SCL) |
| 4x4 Keypad | Digital | Rows: 9, 8, 7, 6 | Cols: 5, 4, 3, 2 |
| RGB LED | Digital | 11 (R), 12 (G), 13 (B) |

## Firmware State Machine

1. **FIRST_RUN:** No PIN is set in EEPROM. User is prompted to set and confirm a 4-digit PIN.
2. **LOCKED:** PIN is set. Serial bridge is disabled. Red LED pulses. User must enter PIN.
3. **UNLOCKED:** PIN verified. Serial bridge active. Green LED on. Python client can now connect.
4. **LOCKOUT:** 3 failed PIN attempts. Device freezes and requires manual reset. Red LED blinks fast.

## EEPROM Memory Map (1024 Bytes)

| Bytes | Purpose | Managed By | Description |
|---|---|---|---|
| 0 | PIN Flag | Firmware | `0xAA` if PIN is set |
| 1 | Fail Count | Firmware | Increments on wrong PIN; triggers lockout at 3 |
| 2–5 | PIN Digits | Firmware | 4 bytes, XOR'd with `0x55` for basic obfuscation |
| 6–7 | Reserved | - | - |
| 8–10 | Magic Header | Python | `0xDE 0xAD 0xBE` marks storage as initialized |
| 11 | Slot Count | Python | Number of credentials stored (0–4) |
| 12–27 | Salt | Python | 16-byte random salt for PBKDF2 |
| 28–31 | Reserved | - | - |
| 32–1023 | Slots | Python | 4 slots of 240 bytes each |

**Note:** The firmware explicitly blocks serial `WRITE` commands to addresses 0–7 to prevent a compromised PC from bypassing the hardware PIN.

## Security Constraints

- **No Plaintext:** No passwords or master keys are ever stored on the device or PC in plaintext.
- **Hardware PIN:** Stays on the device. Python never sees it.
- **Master Password:** Stays on the PC. Arduino never sees it.
- **Authentication:** AES-256-GCM provides authenticated encryption, ensuring that any tampering with EEPROM data results in a decryption failure.
