# ============================================================
# passkey.py — PassKey PC Client (v2, for upgraded firmware)
# ============================================================
# Requires: pip install pyserial cryptography
#
# Works with both:
#   passkey_device.ino  (Wokwi — OLED + keypad)
#   passkey_bridge.ino  (plain Uno, no display)
#
# IMPORTANT: The device must be UNLOCKED via keypad before
# this script can connect (when using passkey_device.ino).
# ============================================================

import os
import sys
import time
import struct
import serial
import serial.tools.list_ports
from getpass import getpass
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.exceptions import InvalidTag

# Bytes 0-7 are managed by the Arduino (PIN, fail count)
# Bytes 8+  are managed by Python
ADDR_MAGIC       = 8
ADDR_SLOT_COUNT  = 11
ADDR_SALT        = 12
ADDR_SLOTS_START = 32
SLOT_SIZE        = 240
MAX_SLOTS        = 4
SALT_LEN         = 16
NONCE_LEN        = 12
BAUD             = 115200
TIMEOUT          = 2

def find_arduino():
    # Candidates: Wokwi virtual port first, then physical ports
    candidates = ["rfc2217://localhost:4000"] + [p.device for p in serial.tools.list_ports.comports()]
    
    if not candidates:
        print("  No serial ports found at all.")
        return None
        
    print(f"  Checking {len(candidates)} port(s):", ", ".join(candidates))
    for dev in candidates:
        try:
            print(f"    Trying {dev}...", end=" ", flush=True)
            s = serial.serial_for_url(dev, BAUD, timeout=TIMEOUT)
            # Skip bootloader delay for Wokwi
            if not dev.startswith("rfc2217"):
                time.sleep(1.8)
            s.reset_input_buffer()
            s.write(b"PING\n")
            resp = s.readline().decode(errors="ignore").strip()
            if resp == "PASSKEY_OK":
                print("connected!")
                return s
            elif resp == "PASSKEY_LOCKED":
                print("found but LOCKED.")
                print()
                print("  The device is waiting for your keypad PIN.")
                print("  Enter your 4-digit PIN on the Wokwi keypad,")
                print("  press # to confirm, then re-run passkey.py.")
                s.close()
                return None
            else:
                print(f"no response ({repr(resp)})")
                s.close()
        except (serial.SerialException, OSError) as e:
            print(f"error ({e})")
    return None

def read_byte(ser, addr):
    ser.write(f"READ {addr}\n".encode())
    r = ser.readline().decode(errors="ignore").strip()
    if r.startswith("ERR"): raise IOError(f"Read error {addr}: {r}")
    return int(r)

def write_byte(ser, addr, val):
    ser.write(f"WRITE {addr} {val}\n".encode())
    r = ser.readline().decode(errors="ignore").strip()
    if r != "ACK": raise IOError(f"Write error {addr}: {r}")

def read_bytes(ser, addr, n):  return bytes(read_byte(ser, addr+i) for i in range(n))
def write_bytes(ser, addr, d): [write_byte(ser, addr+i, b) for i,b in enumerate(d)]

def disp_oled(ser, text):
    try: ser.write(f"DISP {text}\n".encode()); ser.readline()
    except: pass

def lock_device(ser):
    try: ser.write(b"LOCK\n"); ser.readline()
    except: pass

def derive_key(pw, salt):
    return PBKDF2HMAC(hashes.SHA256(), 32, salt, 200_000).derive(pw.encode())

def encrypt(pt, key):
    n = os.urandom(NONCE_LEN)
    return n + AESGCM(key).encrypt(n, pt.encode(), None)

def decrypt(data, key):
    try: return AESGCM(key).decrypt(data[:NONCE_LEN], data[NONCE_LEN:], None).decode()
    except InvalidTag: raise ValueError("Wrong master password or corrupted data.")

def is_initialised(ser):
    return (read_byte(ser,ADDR_MAGIC)==0xDE and
            read_byte(ser,ADDR_MAGIC+1)==0xAD and
            read_byte(ser,ADDR_MAGIC+2)==0xBE)

def initialise_device(ser, salt):
    print("  Initialising storage...")
    write_byte(ser, ADDR_MAGIC, 0xDE)
    write_byte(ser, ADDR_MAGIC+1, 0xAD)
    write_byte(ser, ADDR_MAGIC+2, 0xBE)
    write_byte(ser, ADDR_SLOT_COUNT, 0)
    write_bytes(ser, ADDR_SALT, salt)
    print("  Done.")

def get_salt(ser):        return read_bytes(ser, ADDR_SALT, SALT_LEN)
def get_slot_count(ser):  return read_byte(ser, ADDR_SLOT_COUNT)
def set_slot_count(ser,n): write_byte(ser, ADDR_SLOT_COUNT, n)
def slot_addr(idx):        return ADDR_SLOTS_START + idx * SLOT_SIZE

def read_slot_raw(ser, idx):
    a = slot_addr(idx)
    ln = struct.unpack(">H", read_bytes(ser, a, 2))[0]
    if ln == 0 or ln > SLOT_SIZE-2: return None
    d = read_bytes(ser, a+2, ln)
    return d[:32].rstrip(b"\x00").decode(errors="ignore"), d[32:]

def write_slot(ser, idx, label, payload):
    lb = label.encode()[:32].ljust(32, b"\x00")
    d  = lb + payload
    a  = slot_addr(idx)
    write_bytes(ser, a, struct.pack(">H", len(d)))
    write_bytes(ser, a+2, d)

def clear_slot(ser, idx): write_bytes(ser, slot_addr(idx), bytes(SLOT_SIZE))

def action_list(ser):
    c = get_slot_count(ser)
    if not c: print("  No credentials stored yet."); return
    print(f"\n  Stored slots ({c}/{MAX_SLOTS}):")
    for i in range(c):
        r = read_slot_raw(ser, i)
        print(f"    [{i}]  {r[0] if r else '(empty)'}")

def action_store(ser):
    c = get_slot_count(ser)
    if c >= MAX_SLOTS: print(f"  Device full."); return
    label = input("  Label: ").strip()
    pw    = getpass("  Password to store: ")
    master = getpass("  Master password: ")
    key    = derive_key(master, get_salt(ser))
    print("  Encrypting...", end=" ", flush=True)
    payload = encrypt(pw, key)
    print("done.")
    disp_oled(ser, label)
    print("  Writing...", end=" ", flush=True)
    write_slot(ser, c, label, payload)
    set_slot_count(ser, c+1)
    print(f"done. Stored in slot {c}.")

def action_retrieve(ser):
    action_list(ser)
    c = get_slot_count(ser)
    if not c: return
    try: idx = int(input("  Slot number: "))
    except ValueError: print("  Invalid."); return
    if not (0 <= idx < c): print("  Out of range."); return
    master = getpass("  Master password: ")
    key    = derive_key(master, get_salt(ser))
    r = read_slot_raw(ser, idx)
    if not r: print("  Slot empty or corrupted."); return
    label, payload = r
    disp_oled(ser, label)
    try:
        print(f"\n  Label:    {label}")
        print(f"  Password: {decrypt(payload, key)}\n")
    except ValueError as e:
        print(f"\n  ERROR: {e}")

def action_delete(ser):
    action_list(ser)
    c = get_slot_count(ser)
    if not c: return
    try: idx = int(input("  Slot to delete: "))
    except ValueError: print("  Invalid."); return
    if not (0 <= idx < c): print("  Out of range."); return
    if input(f"  Delete slot {idx}? (yes/no): ").strip().lower() != "yes":
        print("  Cancelled."); return
    for i in range(idx, c-1):
        r = read_slot_raw(ser, i+1)
        if r: write_slot(ser, i, r[0], r[1])
        else: clear_slot(ser, i)
    clear_slot(ser, c-1)
    set_slot_count(ser, c-1)
    print(f"  Slot {idx} deleted.")

def action_reset(ser):
    if input("  Type RESET to erase all: ").strip() != "RESET":
        print("  Cancelled."); return
    print("  Resetting...")
    for i in range(MAX_SLOTS): clear_slot(ser, i)
    for a in range(8, ADDR_SLOTS_START): write_byte(ser, a, 0)
    print("  Done. Restart passkey.py to reinitialise.")

def main():
    print("=" * 46)
    print("  PassKey — Hardware Password Manager v2")
    print("=" * 46)
    print("\n  Scanning for device...")
    print("  (Unlock the device via keypad first)\n")

    ser = find_arduino()
    if not ser:
        print("\n  ERROR: Device not found.")
        print("  - Unlock with your keypad PIN first.")
        print("  - Close Arduino IDE Serial Monitor.")
        sys.exit(1)

    if not is_initialised(ser):
        print("\n  First run — setting up encrypted storage.")
        master  = getpass("  New master password: ")
        confirm = getpass("  Confirm: ")
        if master != confirm:
            print("  Mismatch. Exiting."); ser.close(); sys.exit(1)
        initialise_device(ser, os.urandom(SALT_LEN))

    try:
        while True:
            print("\n" + "-"*46)
            print("  [1] Store  [2] Retrieve  [3] List")
            print("  [4] Delete  [5] Reset  [Q] Quit")
            print("-"*46)
            c = input("  Choose: ").strip().upper()
            if   c=="1": action_store(ser)
            elif c=="2": action_retrieve(ser)
            elif c=="3": action_list(ser)
            elif c=="4": action_delete(ser)
            elif c=="5": action_reset(ser)
            elif c=="Q":
                print("  Locking device...")
                lock_device(ser); ser.close()
                print("  Goodbye."); break
            else: print("  Unknown option.")
    except KeyboardInterrupt:
        lock_device(ser); ser.close()
        print("\n  Interrupted. Device locked.")

if __name__ == "__main__":
    main()
