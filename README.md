# PollMaster
PollMaster is an Arduino-based embedded voting &amp; decision system for classrooms, events, and teams. Uses RC522 RFID (one-card-one-vote), 4×4 keypad input, 16×2 I2C LCD UI, RGB status LED, and buzzer feedback. Admin starts/closes sessions; results and tie detection are shown only after voting ends.

Tech Stack: Arduino Uno + RC522 RFID + 4×4 Keypad + 16×2 I2C LCD + RGB LED + Buzzer
Libraries: MFRC522, SPI, Keypad, LiquidCrystal_I2C

Features

RFID authentication (one-card-one-vote)

Admin session control (start/close/reset)

LCD instructions + RGB/Buzzer feedback

Automatic results + tie/winner detection

Wiring

RC522: SS=D10, SCK=D13, MOSI=D11, MISO=D12, RST=D9, VCC=3.3V, GND=GND

LCD I2C: SDA=A4, SCL=A5
