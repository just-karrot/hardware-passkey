// ============================================================
// passkey.ino — PassKey Upgraded Firmware (LCD 1602 Edition)
// ============================================================
// Hardware: Arduino Uno + I2C LCD 1602 + 4x4 Keypad + RGB LED
//
// Libraries required:
//   Keypad            (by Mark Stanley, Alexander Brevig)
//   LiquidCrystal I2C (by Frank de Brabander)
//
// Pin map:
//   LCD   SDA → SDA (A4)   LCD   SCL → SCL (A5)
//   Keypad rows  → 9, 8, 7, 6
//   Keypad cols  → 5, 4, 3, 2
//   RGB R → 11   G → 12   B → 13   (each via 220Ω to GND)
// ============================================================

#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

// ── LCD ─────────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Keypad ──────────────────────────────────────────────────
const byte KP_ROWS = 4, KP_COLS = 4;
char keys[KP_ROWS][KP_COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[KP_ROWS] = {9, 8, 7, 6};
byte colPins[KP_COLS] = {5, 4, 3, 2};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, KP_ROWS, KP_COLS);

// ── RGB LED ─────────────────────────────────────────────────
#define PIN_R 11
#define PIN_G 12
#define PIN_B 13

// ── EEPROM addresses ─────────────────────────────────────────
#define ADDR_PIN_FLAG    0
#define ADDR_FAIL_COUNT  1
#define ADDR_PIN_DIGITS  2
#define ADDR_MAGIC       8
#define PIN_OBFUSC       0x55
#define MAX_FAILS        3
#define PIN_LEN          4

// ── States ──────────────────────────────────────────────────
enum State { FIRST_RUN, LOCKED, UNLOCKED, LOCKOUT };
State state;

// ── PIN entry buffer ─────────────────────────────────────────
char pinBuffer[PIN_LEN + 1];
byte pinLen = 0;

// ── LED helpers ──────────────────────────────────────────────
void setLED(bool r, bool g, bool b) {
  digitalWrite(PIN_R, r ? HIGH : LOW);
  digitalWrite(PIN_G, g ? HIGH : LOW);
  digitalWrite(PIN_B, b ? HIGH : LOW);
}
void ledRed()   { setLED(1, 0, 0); }
void ledGreen() { setLED(0, 1, 0); }
void ledBlue()  { setLED(0, 0, 1); }
void ledOff()   { setLED(0, 0, 0); }

unsigned long lastBlink = 0;
bool blinkState = false;
void blinkLED(bool r, bool g, bool b, int interval) {
  if (millis() - lastBlink > (unsigned long)interval) {
    blinkState = !blinkState;
    setLED(blinkState ? r : 0, blinkState ? g : 0, blinkState ? b : 0);
    lastBlink = millis();
  }
}

// ── LCD screens ──────────────────────────────────────────────
void showLocked() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PassKey [LOCKED]");
  lcd.setCursor(0, 1);
  lcd.print("PIN:");
  for(int i=0; i<pinLen; i++) lcd.print('*');
  lcd.setCursor(11, 1);
  lcd.print("#=OK");
}

void showFirstRun(bool confirm) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(confirm ? "Confirm PIN:" : "Setup: New PIN");
  lcd.setCursor(0, 1);
  lcd.print("PIN:");
  for(int i=0; i<pinLen; i++) lcd.print('*');
  lcd.setCursor(11, 1);
  lcd.print("#=OK");
}

void showUnlocked(const char* msg = "Waiting for PC") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PassKey [ACTIVE]");
  lcd.setCursor(0, 1);
  lcd.print(msg);
}

void showMessage(const char* line1, const char* line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void showLockout() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" SYSTEM LOCKED ");
  lcd.setCursor(0, 1);
  lcd.print("Reset hardware!");
}

void showSlotDisplay(const char* text) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Accessing:");
  lcd.setCursor(0, 1);
  lcd.print(text);
}

// ── PIN storage ──────────────────────────────────────────────
void savePin(const char* pin) {
  for (byte i = 0; i < PIN_LEN; i++) {
    EEPROM.update(ADDR_PIN_DIGITS + i, (byte)(pin[i] ^ PIN_OBFUSC));
  }
  EEPROM.update(ADDR_PIN_FLAG,   0xAA);
  EEPROM.update(ADDR_FAIL_COUNT, 0);
}

bool checkPin(const char* entered) {
  for (byte i = 0; i < PIN_LEN; i++) {
    byte stored = EEPROM.read(ADDR_PIN_DIGITS + i) ^ PIN_OBFUSC;
    if ((byte)entered[i] != stored) return false;
  }
  return true;
}

bool isPinSet()    { return EEPROM.read(ADDR_PIN_FLAG) == 0xAA; }
bool isLockedOut() { return EEPROM.read(ADDR_FAIL_COUNT) >= MAX_FAILS; }

void incrementFails() {
  byte f = EEPROM.read(ADDR_FAIL_COUNT);
  EEPROM.update(ADDR_FAIL_COUNT, f + 1);
}
void resetFails() { EEPROM.update(ADDR_FAIL_COUNT, 0); }

// ── Serial bridge ────────────────────────────────────────────
void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  ledBlue(); delay(40); ledGreen();

  if (line == "PING") {
    Serial.println("PASSKEY_OK");
    return;
  }

  if (line.startsWith("READ ")) {
    int addr = line.substring(5).toInt();
    if (addr >= 0 && addr < (int)EEPROM.length()) {
      Serial.println(EEPROM.read(addr));
    } else {
      Serial.println("ERR_ADDR");
    }
    return;
  }

  if (line.startsWith("WRITE ")) {
    int sp1  = line.indexOf(' ');
    int sp2  = line.indexOf(' ', sp1 + 1);
    int addr = line.substring(sp1 + 1, sp2).toInt();
    int val  = line.substring(sp2 + 1).toInt();
    if (addr >= 0 && addr < (int)EEPROM.length() && val >= 0 && val <= 255) {
      if (addr >= 8) {
        EEPROM.update(addr, (byte)val);
      }
      Serial.println("ACK");
    } else {
      Serial.println("ERR_RANGE");
    }
    return;
  }

  if (line.startsWith("DISP ")) {
    String msg = line.substring(5);
    showSlotDisplay(msg.c_str());
    Serial.println("ACK");
    return;
  }

  if (line == "LOCK") {
    Serial.println("ACK");
    Serial.end();
    state = LOCKED;
    pinLen = 0;
    ledRed();
    showLocked();
    return;
  }
  Serial.println("ERR_CMD");
}

bool readKeypadDigit(bool confirmMode) {
  char key = keypad.getKey();
  if (!key) return false;
  if (key == '*') {
    if (pinLen > 0) pinLen--;
    confirmMode ? showFirstRun(true) : showLocked();
    return false;
  }
  if (key == '#') {
    if (pinLen == PIN_LEN) return true;
    return false;
  }
  if (key >= '0' && key <= '9' && pinLen < PIN_LEN) {
    pinBuffer[pinLen++] = key;
    pinBuffer[pinLen]   = '\0';
    confirmMode ? showFirstRun(true) : showLocked();
  }
  return false;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);

  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(4, 0);
  lcd.print("PassKey");
  lcd.setCursor(0, 1);
  lcd.print("Hardware Vault v2");
  delay(1800);

  if (!isPinSet()) {
    state = FIRST_RUN;
    pinLen = 0;
    ledOff();
    showFirstRun(false);
  } else if (isLockedOut()) {
    state = LOCKOUT;
    ledRed();
    showLockout();
  } else {
    state = LOCKED;
    pinLen = 0;
    ledRed();
    showLocked();
  }
}

void handlePingAlways() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line == "PING") {
    if (state == UNLOCKED) Serial.println("PASSKEY_OK");
    else                   Serial.println("PASSKEY_LOCKED");
  }
}

void loop() {
  if (state != UNLOCKED) handlePingAlways();

  switch (state) {
    case LOCKOUT:
      blinkLED(1, 0, 0, 300);
      break;

    case FIRST_RUN: {
      static char firstPin[PIN_LEN + 1] = "";
      static bool waitingConfirm = false;
      static byte lastLen = 255;

      if (!waitingConfirm) {
        if (lastLen != pinLen) { showFirstRun(false); lastLen = pinLen; }
        if (readKeypadDigit(false)) {
          strncpy(firstPin, pinBuffer, PIN_LEN);
          firstPin[PIN_LEN] = '\0';
          pinLen = 0;
          lastLen = 255;
          waitingConfirm = true;
          showFirstRun(true);
        }
      } else {
        if (lastLen != pinLen) { showFirstRun(true); lastLen = pinLen; }
        if (readKeypadDigit(true)) {
          if (strcmp(pinBuffer, firstPin) == 0) {
            savePin(firstPin);
            pinLen = 0;
            waitingConfirm = false;
            showMessage("PIN set!", "Locking...");
            ledGreen(); delay(1200); ledRed();
            state = LOCKED;
            showLocked();
          } else {
            pinLen = 0;
            waitingConfirm = false;
            showMessage("Mismatch!", "Try again.");
            ledRed(); delay(1200);
            showFirstRun(false);
          }
        }
      }
      break;
    }

    case LOCKED: {
      static byte lastLockedLen = 255;
      blinkLED(1, 0, 0, 600);
      if (lastLockedLen != pinLen) { showLocked(); lastLockedLen = pinLen; }
      if (readKeypadDigit(false)) {
        if (checkPin(pinBuffer)) {
          resetFails();
          pinLen = 0;
          state = UNLOCKED;
          ledGreen();
          showUnlocked();
        } else {
          incrementFails();
          int remaining = MAX_FAILS - EEPROM.read(ADDR_FAIL_COUNT);
          if (remaining <= 0) {
            state = LOCKOUT;
            ledRed();
            showLockout();
          } else {
            char msg[17];
            snprintf(msg, sizeof(msg), "%d attempts left", remaining);
            showMessage("Wrong PIN!", msg);
            ledRed(); delay(1500);
            pinLen = 0;
            lastLockedLen = 255;
            showLocked();
          }
        }
      }
      break;
    }

    case UNLOCKED:
      ledGreen();
      handleSerial();
      break;
  }
}
