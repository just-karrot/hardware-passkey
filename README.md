# PassKey — Hardware Password Vault (v2)

A robust, hardware-secured password manager combining an Arduino Uno (physical or simulated) and a Python-based PC client.

Passwords are never stored in plaintext. They are encrypted on your PC using **AES-256-GCM** with a master password — the Arduino acts as a physical gatekeeper, requiring a **Hardware PIN** and storing only the encrypted "vault" in its EEPROM.

---

## 🔒 Security Model

1.  **Physical Tier (Hardware PIN):** You must enter a 4-digit PIN on the device keypad to unlock communication. After 3 failed attempts, the device enters a `LOCKOUT` state.
2.  **Encryption Tier (Master Password):** Passwords are encrypted via AES-256-GCM. The key is derived using PBKDF2-SHA256 (200,000 iterations) from your master password and a unique 16-byte salt stored on the device.
3.  **Zero-Knowledge:** The PC never sees the hardware PIN, and the Arduino never sees your master password.

---

## 🛠️ Hardware Simulation (Wokwi)

The project is pre-configured for the **Wokwi VS Code extension**. It simulates:
- **Arduino Uno**
- **I2C LCD 1602** (Status and prompts)
- **4x4 Membrane Keypad** (PIN entry)
- **RGB LED** (Status: Red=Locked, Green=Unlocked, Blue=Activity)

---

## 🚀 Setup & Usage

### 1. Requirements
- [VS Code](https://code.visualstudio.com/) with the **Wokwi Simulation** extension.
- Python 3.8+
- `arduino-cli` (installed in the project root for local compilation).

### 2. Installation
Install the necessary Python libraries:
```bash
pip install pyserial cryptography
```

Install the Arduino libraries for local compilation:
```bash
./arduino-cli.exe lib install "Keypad" "LiquidCrystal I2C"
```

### 3. Compile & Simulate
Compile the firmware into the `build` folder:
```bash
./arduino-cli.exe compile --fqbn arduino:avr:uno ./passkey --build-path ./passkey/build
```

1.  Open `passkey/diagram.json` in VS Code.
2.  Press **F1** and select **"Wokwi: Start Simulator"**.
3.  The LCD should light up. If it's your first run, follow the prompts to set a new 4-digit PIN.

### 4. Run the Client
1.  On the simulated keypad, enter your 4-digit PIN and press `#`.
2.  Once the LCD shows **"Waiting for PC"**, ensure the Wokwi tab is focused.
3.  Run the Python client:
    ```bash
    python passkey/passkey.py
    ```

---

## 📂 Project Structure

- `passkey/passkey.py`: The PC client (Encryption/Decryption/UI).
- `passkey/passkey.ino`: Upgraded firmware with LCD and Keypad support.
- `passkey/diagram.json`: Wokwi hardware configuration.
- `passkey/wokwi.toml`: Simulator settings (including the RFC2217 serial bridge).
- `GEMINI.md`: Detailed architecture and memory map for developers.

---

## 🧠 EEPROM Memory Map (1024 Bytes)

| Bytes | Purpose | Description |
|---|---|---|
| 0 | PIN Flag | `0xAA` if PIN is set |
| 1 | Fail Count | Lockout at 3 failed attempts |
| 2–5 | PIN Digits | 4-digit PIN (obfuscated) |
| 8–10 | Magic Header | `0xDE 0xAD 0xBE` |
| 11 | Slot Count | 0–4 stored credentials |
| 12–27 | Salt | 16-byte random salt for encryption |
| 32+ | Slots | 4 slots (240 bytes each) |

---

## ⚠️ Important Notes
- **Do not forget your Master Password.** There is no recovery mechanism.
- **Wokwi Focus:** The simulation window must be the active tab in VS Code for the serial communication to work correctly.
- **Local Build:** Always recompile after making changes to `passkey.ino` for the simulator to pick them up.
