#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>

// =======================
// LCD (I2C)
// =======================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =======================
// RFID RC522 (SPI)
// =======================
#define SS_PIN 10
#define RST_PIN 8
MFRC522 rfid(SS_PIN, RST_PIN);

// =======================
// RGB + Buzzer
// =======================
const int redPin = 5;
const int greenPin = 6;
const int bluePin = 3;
const bool COMMON_ANODE = false;

#define buzzerPin 4

// =======================
// Voting state
// =======================
int votes[3] = {0, 0, 0}; // A, B, C
bool votingActive = false;
bool votingClosed = false;
bool adminAccess = false;

// =======================
// Keypad mapping
// Rows: A0 A1 A2 A3
// Cols: D0 D1 D2 D7   (NOTE: D0/D1 are Serial pins on UNO)
// =======================
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {A0, A1, A2, A3};
byte colPins[COLS] = {0, 1, 2, 7};   // D0,D1,D2,D7
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// =======================
// RFID One-card-one-vote (RAM)
// =======================
struct UID4 { byte b[4]; };
const int MAX_UIDS = 200;
UID4 usedUIDs[MAX_UIDS];
int usedCount = 0;

bool hasActiveCard = false;
UID4 activeUID;

// =======================
// Admin password (digits only; confirm with #)
// =======================
String password = "1234";
String input = "";
int wrongTries = 0;
unsigned long lockUntil = 0;

// =======================
// Helpers
// =======================
bool sameUID(const UID4 &a, const UID4 &b) {
  for (int i = 0; i < 4; i++) if (a.b[i] != b.b[i]) return false;
  return true;
}

bool isUsedUID(const UID4 &u) {
  for (int i = 0; i < usedCount; i++) {
    if (sameUID(usedUIDs[i], u)) return true;
  }
  return false;
}

void markUsedUID(const UID4 &u) {
  if (usedCount < MAX_UIDS) usedUIDs[usedCount++] = u;
}

// =======================
// Sounds
// =======================
void beepShort() {
  digitalWrite(buzzerPin, HIGH);
  delay(100);
  digitalWrite(buzzerPin, LOW);
}

void beepDouble() {
  for (int i = 0; i < 2; i++) { beepShort(); delay(100); }
}

void buzzLong() {
  digitalWrite(buzzerPin, HIGH);
  delay(700);
  digitalWrite(buzzerPin, LOW);
}

void rejectTone() {
  for (int i = 0; i < 3; i++) { beepShort(); delay(80); }
}

void winCelebration() {
  for (int i = 0; i < 3; i++) { beepShort(); delay(150); }
}

void tieTone() {
  buzzLong();
  delay(200);
  beepShort();
}

// =======================
// RGB
// =======================
void setRGB(int r, int g, int b) {
  if (COMMON_ANODE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(redPin, r);
  analogWrite(greenPin, g);
  analogWrite(bluePin, b);
}

void pulseWhite() {
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j <= 255; j += 25) { setRGB(j, j, j); delay(20); }
    for (int j = 255; j >= 0; j -= 25) { setRGB(j, j, j); delay(20); }
  }
}

// =======================
// UI Screens
// =======================
void waitingScreen() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("PollMaster Sys");
  lcd.setCursor(0,1);
  lcd.print("PIN then #=OK");
  setRGB(0, 0, 30);
}

void adminMenu() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("1Start 2Close");
  lcd.setCursor(0,1);
  lcd.print("0View  4Reset");
}

void votingIdleScreen() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Tap RFID to vote");
  lcd.setCursor(0,1);
  lcd.print("Vote: A/B/C");
  setRGB(0, 60, 0);
}

void showLiveCounts() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("A:"); lcd.print(votes[0]);
  lcd.print(" B:"); lcd.print(votes[1]);
  lcd.setCursor(0,1);
  lcd.print("C:"); lcd.print(votes[2]);
}

void startCountdown() {
  for (int i = 3; i > 0; i--) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Voting starts in");
    lcd.setCursor(0,1);
    lcd.print(i);
    beepShort();
    delay(1000);
  }
  votingActive = true;
  votingClosed = false;
  hasActiveCard = false;
  votingIdleScreen();
}

void resetSession() {
  for (int i = 0; i < 3; i++) votes[i] = 0;
  usedCount = 0;
  hasActiveCard = false;
  votingActive = false;
  votingClosed = false;
  adminAccess = false;

  input = "";
  wrongTries = 0;
  lockUntil = 0;

  lcd.clear();
  lcd.print("Session Reset");
  buzzLong();
  delay(900);
  waitingScreen();
}

// =======================
// RFID read (robust)
// =======================
bool readCardUID(UID4 &out) {
  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial()) return false;

  for (int i = 0; i < 4; i++) out.b[i] = rfid.uid.uidByte[i];

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return true;
}

void handleRFID() {
  if (!votingActive || votingClosed) return;

  UID4 u;
  if (!readCardUID(u)) return;

  beepShort();

  if (isUsedUID(u)) {
    lcd.clear();
    lcd.print("Already Voted!");
    setRGB(255, 80, 0);
    rejectTone();
    delay(900);
    votingIdleScreen();
    return;
  }

  activeUID = u;
  hasActiveCard = true;

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Card OK - Vote");
  lcd.setCursor(0,1);
  lcd.print("Press A/B/C");
  setRGB(0, 120, 120);
}

// =======================
// Voting logic
// =======================
void castVote(int candidate) {
  if (!votingActive || votingClosed) return;

  if (!hasActiveCard) {
    lcd.clear();
    lcd.print("Tap RFID first!");
    rejectTone();
    delay(900);
    votingIdleScreen();
    return;
  }

  if (candidate < 0 || candidate > 2) return;

  votes[candidate]++;
  markUsedUID(activeUID);
  hasActiveCard = false;

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Vote Accepted!");
  lcd.setCursor(0,1);
  lcd.print("Candidate ");
  lcd.print((char)('A' + candidate));
  setRGB(0, 255, 0);
  beepShort();
  delay(1000);
  votingIdleScreen();
}

void showResults() {
  showLiveCounts();
  delay(2000);

  int maxVotes = votes[0];
  for (int i = 1; i < 3; i++) if (votes[i] > maxVotes) maxVotes = votes[i];

  int winners[3] = {0,0,0};
  int tieCount = 0;
  for (int i = 0; i < 3; i++) {
    if (votes[i] == maxVotes) { winners[i] = 1; tieCount++; }
  }

  lcd.clear();
  if (tieCount > 1) {
    lcd.setCursor(0,0);
    lcd.print("Tie between:");
    lcd.setCursor(0,1);
    if (winners[0]) lcd.print("A ");
    if (winners[1]) lcd.print("B ");
    if (winners[2]) lcd.print("C ");
    tieTone();

    if (tieCount == 3) pulseWhite();
    else {
      int r = winners[0] ? 255 : 0;
      int g = winners[1] ? 255 : 0;
      int b = winners[2] ? 255 : 0;
      setRGB(r, g, b);
    }
  } else {
    int winner = 0;
    for (int i = 0; i < 3; i++) if (winners[i]) winner = i;
    lcd.setCursor(0,0);
    lcd.print("Winner: ");
    lcd.print((char)('A' + winner));

    if (winner == 0) setRGB(255, 0, 0);
    else if (winner == 1) setRGB(0, 255, 0);
    else setRGB(0, 0, 255);

    winCelebration();
  }

  delay(1500);
  resetSession();
}

// =======================
// Admin PIN handler (Keypad)
// Confirm = #
// Clear   = *
// Digits allowed: 0..9 (normal)
// =======================
void handleAdminPinKey(char key) {
  if (votingActive || votingClosed || adminAccess) return;

  if (millis() < lockUntil) {
    lcd.setCursor(0,1);
    lcd.print("LOCKED...      ");
    return;
  }

  if (!key) return;

  if (key == '#') {   // Confirm
    if (input == password) {
      adminAccess = true;
      wrongTries = 0;
      lcd.clear();
      lcd.print("Access Granted");
      beepDouble();
      delay(700);
      adminMenu();
    } else {
      adminAccess = false;
      wrongTries++;
      lcd.clear();
      lcd.print("Access Denied!");
      buzzLong();

      if (wrongTries >= 3) {
        lockUntil = millis() + 30000UL; // 30 sec
        lcd.clear();
        lcd.print("LOCKED 30 sec");
        rejectTone();
      }

      delay(900);
      waitingScreen();
    }
    input = "";
    return;
  }

  if (key == '*') {   // Clear
    input = "";
    lcd.clear();
    lcd.print("Enter Admin PIN");
    lcd.setCursor(0,1);
    lcd.print("                ");
    return;
  }

  // Normal digits
  if (key >= '0' && key <= '9') {
    if (input.length() < 8) { // just a safe limit
      input += key;
      lcd.setCursor(0,1);
      lcd.print("                ");
      lcd.setCursor(0,1);
      lcd.print(input); // (or print **** if you want)
    }
  }
}

// =======================
// Admin menu handler
// =======================
void handleAdminMenuKey(char key) {
  if (!adminAccess || votingActive || votingClosed) return;
  if (!key) return;

  if (key == '1') startCountdown();
  else if (key == '4') resetSession();
  // keys '0' and '2' are used during voting (view/close)
}

// =======================
// Voting keypad handler
// =======================
void handleVotingKey(char key) {
  if (!votingActive || votingClosed) return;
  if (!key) return;

  if (key == 'A') castVote(0);
  else if (key == 'B') castVote(1);
  else if (key == 'C') castVote(2);

  // Admin during voting
  else if (adminAccess && key == '0') {
    showLiveCounts();
    delay(900);
    votingIdleScreen();
  }
  else if (adminAccess && key == '2') {
    votingClosed = true;
    votingActive = false;
    lcd.clear();
    lcd.print("Voting Closed");
    beepShort();
    delay(500);
    showResults();
  }
  else if (adminAccess && key == '4') {
    resetSession();
  }
}

// =======================
// Setup / Loop
// =======================
void setup() {
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  lcd.init();
  lcd.backlight();

  // --- RFID init ---
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);

  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV8);  // ~2MHz
  rfid.PCD_Init();
  delay(50);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("RFID Init OK?");
  lcd.setCursor(0,1);
  lcd.print("Tap card test");
  setRGB(0, 0, 60);
  delay(1200);

  waitingScreen();
}

void loop() {
  // Read keypad ONCE per loop (fixes keypad behaving weird)
  char key = keypad.getKey();

  // 1) Enter admin PIN when not in voting and not already admin
  handleAdminPinKey(key);

  // 2) Admin menu commands before voting
  handleAdminMenuKey(key);

  // 3) During voting: RFID + keypad voting
  if (votingActive && !votingClosed) {
    handleRFID();          // RFID first
    handleVotingKey(key);  // then keypad
  }
}
