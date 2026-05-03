# PassKey - Hardware Password Vault (v2)

A robust, hardware-secured password manager combining an Arduino Uno (physical or simulated) and a Python-based PC client.

Passwords are never stored in plaintext. They are encrypted on your PC using **AES-256-GCM** with a master password: the Arduino acts as a physical gatekeeper, requiring a **Hardware PIN** and storing only the encrypted "vault" in its EEPROM.

---

## Security Model

1.  **Physical Tier (Hardware PIN):** You must enter a 4-digit PIN on the device keypad to unlock communication. After 3 failed attempts, the device enters a `LOCKOUT` state.
2.  **Encryption Tier (Master Password):** Passwords are encrypted via AES-256-GCM. The key is derived using PBKDF2-SHA256 (200,000 iterations) from your master password and a unique 16-byte salt stored on the device.
3.  **Zero-Knowledge:** The PC never sees the hardware PIN, and the Arduino never sees your master password.

---

## Hardware Simulation (Wokwi)

The project is pre-configured for the **Wokwi VS Code extension**. It simulates:
- **Arduino Uno**
- **I2C LCD 1602** (Status and prompts)
- **4x4 Membrane Keypad** (PIN entry)
- **RGB LED** (Status: Red=Locked, Green=Unlocked, Blue=Activity)

---

## Installation and Prerequisites

### 1. Wokwi VS Code Extension
- Install the **Wokwi Simulation** extension from the VS Code Marketplace.
- Press `F1` in VS Code and select **Wokwi: Request a new License** to activate the advanced features (like the serial bridge).

### 2. Arduino CLI
The local simulation requires `arduino-cli` to compile the firmware.
- **Windows:** Download the latest `arduino-cli` executable and place it in the project root.
- **Setup Core:** Run `./arduino-cli.exe core install arduino:avr`
- **Setup Libraries:** Run:
  ```powershell
  ./arduino-cli.exe lib install "Keypad"
  ./arduino-cli.exe lib install "LiquidCrystal I2C"
  ```

### 3. Python Environment
- Python 3.8+ is required.
- Install dependencies: `pip install pyserial cryptography`

---

## Serial Bridge Configuration

Because the simulation runs inside VS Code, it does not create a physical COM port. Instead, it uses an **RFC2217 Serial Server**.

- **Configuration:** This is enabled in `passkey/wokwi.toml` via `rfc2217ServerPort = 4000`.
- **Port Remapping:** The Python client is configured to check `rfc2217://localhost:4000` before attempting to scan physical COM ports.

---

## Compilation and Deployment

To update the simulation with code changes:

1.  **Compile:** Run the following command from the project root:
    ```powershell
    ./arduino-cli.exe compile --fqbn arduino:avr:uno ./passkey --build-path ./passkey/build
    ```
2.  **Run Simulation:** Open `passkey/diagram.json`, press `F1`, and select **Wokwi: Start Simulator**.
3.  **Unlock:** Enter your 4-digit PIN on the simulated keypad and press `#`.

---

## Running the PC Client

1.  Ensure the Arduino LCD shows **"Waiting for PC"**.
2.  **Crucial:** Click on the Wokwi simulation tab in VS Code so it remains the **active/focused window**.
3.  Execute the client:
    ```bash
    python passkey/passkey.py
    ```

---

## Troubleshooting and Debugging

- **LCD/LED Not Lighting Up:** Ensure `arduino-cli` compiled successfully and that `passkey/wokwi.toml` points to the correct `.hex` and `.elf` files in the `build` directory.
- **Serial Timeout (No Response):** The Wokwi extension pauses serial processing if its tab is not focused. Ensure the simulation tab is visible and active when running the Python script.
- **I2C Initialization Error:** If the hardware fails to boot (Fast Red Blink), verify that the I2C address in `passkey/passkey.ino` matches the `i2cAddress` in `passkey/diagram.json` (Default: `0x27`).
- **Git Errors:** If `git push` is rejected, use `git pull --rebase origin main` to sync remote changes before pushing.

---

## EEPROM Memory Map (1024 Bytes)

| Bytes | Purpose | Description |
|---|---|---|
| 0 | PIN Flag | `0xAA` if PIN is set |
| 1 | Fail Count | Lockout at 3 failed attempts |
| 2-5 | PIN Digits | 4-digit PIN (obfuscated) |
| 8-10 | Magic Header | `0xDE 0xAD 0xBE` |
| 11 | Slot Count | 0-4 stored credentials |
| 12-27 | Salt | 16-byte random salt for encryption |
| 32+ | Slots | 4 slots (240 bytes each) |
