#include <Keypad.h>
#include <MFRC522.h>
#include <waveshare_fingerprint.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <EEPROM.h>

#define BUFF_SIZE 5
#define EEPROM_START_ADDR 10
#define EEPROM_KEY 33
#define MAX_TAGS 10
#define MAX_FP 10
#define RC522_CS 10
#define RC522_RST 9
#define FPR_RX 14
#define FPR_TX 15
#define FPR_RST 16
#define FPR_WAKE 17
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 3
#define PASSWORD_LENGTH 7
#define SERVO_PIN 18
#define BUZZER_PIN 19

const char keys[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

byte rowPins[KEYPAD_ROWS] = {8, 7, 6, 5};
byte colPins[KEYPAD_COLS] = {4, 3, 2};

char code[PASSWORD_LENGTH];
uint8_t pos = 0;
uint8_t tagCount = 0;
uint16_t slot = 0;
bool locked;
bool needClear = false;
uint32_t eeprom_code;
uint32_t status_tmr, peripheral_tmr, keypad_tmr, pcid_tmr, lock_tmr;

enum states{
  RUN = 0,
  PSW_CHANGE,
  RFID_ADD,
  RFID_DEL,
  FPR_ADD,
  FPR_DEL
} state;

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);
SoftwareSerial fpr_serial(FPR_RX, FPR_TX);
waveshare_fingerprint fpr(&fpr_serial, FPR_RST, FPR_WAKE);
MFRC522 rfid(RC522_CS, RC522_RST);
Servo servo;

void setup() {
  Serial.begin(19200);
  SPI.begin();
  if (needClear or EEPROM.read(EEPROM_START_ADDR) != EEPROM_KEY) {
    for (uint16_t i = 0; i < EEPROM.length(); i++) EEPROM.update(i, 0x00);
    EEPROM.write(EEPROM_START_ADDR, EEPROM_KEY);
  } else {
    tagCount = EEPROM.read(EEPROM_START_ADDR + 1);
  }
  lock();
  pinMode(BUZZER_PIN, OUTPUT);
  fpr.begin();
  fpr.allow_overwrite(false);
  fpr.set_timeout(12);
  rfid.PCD_Init();
}

void loop() {
  readLockStatus();
  resetPCID();
  switch (state){
    case RUN:
      esp_receive();
      if (!fpr.is_sleep_scan())
        fpr.begin_sleep_scan();
      if (locked){
        //------------------------------------FPR CODE----------------------------------
      
        slot = 0;
        fpr.sleep_1_to_N_scan(&slot);
        if (slot > 0 and slot != 0xFFFF){
          signalize(true);
          unlock();
          Serial.write("WAS_UNLOCKED");
        }
        else if (slot == 0xFFFF) signalize(false);

        //-----------------------------------RFID CODE----------------------------------
      
        if (rfid.PICC_IsNewCardPresent() and rfid.PICC_ReadCardSerial()){
          if (findTag(rfid.uid.uidByte, rfid.uid.size) >= 0){
            signalize(true);
            unlock();
            Serial.write("WAS_UNLOCKED");
          }
          else signalize(false);
        }

        //---------------------------------KEYPAD CODE----------------------------------
      
        char key = keypad.getKey();
        if ((millis() - keypad_tmr > 30000) || (key == '*') || pos == (PASSWORD_LENGTH - 1)){
          keypad_tmr = millis();
          memset(code, 0, PASSWORD_LENGTH);
          pos = 0;
        }
        else if (key){
          code[pos] = key;
          pos++;
          if (strlen(code) == (PASSWORD_LENGTH - 1)){
            EEPROM.get((MAX_TAGS * 8) + EEPROM_START_ADDR + 2, eeprom_code);
            if (eeprom_code == (uint32_t)strtol(code, NULL, 10)){
              signalize(true);
              unlock();
              Serial.write("WAS_UNLOCKED");
            }
            else{
              signalize(false);
            }
          }
        }
      }
      break;
    case PSW_CHANGE:
      changeCode();
      if (millis() - peripheral_tmr > 30000)
        state = RUN;
      break;
    case RFID_ADD:
      if (rfid.PICC_IsNewCardPresent() and rfid.PICC_ReadCardSerial() and !locked){
        addTag(rfid.uid.uidByte, rfid.uid.size);
        state = RUN;
      }
      else if (millis() - peripheral_tmr > 15000)
        state = RUN;
      break;
    case RFID_DEL:
      if (rfid.PICC_IsNewCardPresent() and rfid.PICC_ReadCardSerial() and !locked){
        deleteTag(rfid.uid.uidByte, rfid.uid.size);
        state = RUN;
      }
      else if (millis() - peripheral_tmr > 15000)
        state = RUN;
      break;
    case FPR_ADD:
      addFP();
      if (millis() - peripheral_tmr > 30000)
        state = RUN;
      break;
    case FPR_DEL:
      deleteFP();
      if (millis() - peripheral_tmr > 15000)
        state = RUN;
      break;
    default:
      state = RUN;
      break; 
  }
}

void unlock(){
  servo.attach(SERVO_PIN);
  servo.write(10);
  delay(1000);
  servo.detach();
}

void lock(){
  servo.attach(SERVO_PIN);
  servo.write(170);
  delay(1000);
  servo.detach();
}

void sendLockStatus(){
  if (locked){
    Serial.write("LOCKED");
  }
  else{
    Serial.write("UNLOCKED");
  }
}

void readLockStatus(){
  servo.attach(SERVO_PIN);
  uint8_t angle = servo.read();
  if (angle == 10){
    servo.detach();
    locked = false; 
  }
  else if (angle == 170){
    servo.detach();
    locked = true; 
  }
  else{ 
    servo.detach();
    locked = false;
  }
}

void signalize(bool success){
  if (success){
    digitalWrite(BUZZER_PIN, HIGH);
    delay(500);
    digitalWrite(BUZZER_PIN, LOW);
  }
  else{
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(300);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void esp_receive(){
  if (Serial.available() > 0){
    byte buff[BUFF_SIZE];
    Serial.readBytesUntil(';', buff, BUFF_SIZE);
    processEspMsg(buff[0]);
  }
}

void processEspMsg(byte esp_msg){
  switch (esp_msg){
    case 1:
      if (!locked){
        lock();
        Serial.write("WAS_LOCKED");
      }
      break;
    case 2:
      if (locked){
        unlock();
        Serial.write("WAS_UNLOCKED");
      }
      break;
    case 3:
      pos = 0;
      state = PSW_CHANGE;
      peripheral_tmr = millis();
      break;
    case 4:
      state = RFID_ADD;
      peripheral_tmr = millis();
      break;
    case 5:
      state = RFID_DEL;
      peripheral_tmr = millis();
      break;
    case 6:
      state = FPR_ADD;
      fpr.end_sleep_scan();
      peripheral_tmr = millis();
      break;
    case 7:
      state = FPR_DEL;
      peripheral_tmr = millis();
      break;
    case 8:
      sendLockStatus();
      break;
    default:
      state = RUN;
      break;
  }
}

bool compareUIDs(uint8_t *tag1, uint8_t *tag2, uint8_t size) {
  for (uint8_t i = 0; i < size; i++) {
    if (tag1[i] != tag2[i]) return false;
  }
  return true;
}

int16_t findTag(uint8_t *tag, uint8_t size) {
  uint8_t buf[8];
  uint16_t address;
  for (uint8_t i = 0; i < tagCount; i++) {
    address = (i * 8) + EEPROM_START_ADDR + 2;
    EEPROM.get(address, buf);
    if (compareUIDs(tag, buf, size)) return address;
  }
  return -1;
}

void deleteTag(uint8_t *tag, uint8_t size){
  int16_t tagAddr = findTag(tag, size);
  uint16_t newTagAddr = (tagCount * 8) + EEPROM_START_ADDR + 2;
  if (tagAddr >= 0) {
    for (uint8_t i = 0; i < 8; i++){
      EEPROM.update(tagAddr + i, EEPROM.read((newTagAddr - 8) + i));
      EEPROM.update((newTagAddr - 8) + i, 0x00);
    }
    EEPROM.write(EEPROM_START_ADDR + 1, --tagCount);
    Serial.write("RFID_DEL");
  }
  else
    Serial.write("RFID_NOT_FOUND");
}

void addTag(uint8_t *tag, uint8_t size){
  int16_t tagAddr = findTag(tag, size);
  uint16_t newTagAddr = (tagCount * 8) + EEPROM_START_ADDR + 2;
  if (tagCount < MAX_TAGS){
    if (tagAddr == -1){
      for (uint16_t i = 0; i < size; i++) 
        EEPROM.update(i + newTagAddr, tag[i]);
      EEPROM.write(EEPROM_START_ADDR + 1, ++tagCount); 
      Serial.write("RFID_ADD");
    }
    else
      Serial.write("RFID_FOUND");
  }
  else
    Serial.write("RFID_MAX");
}

void resetPCID(){
  if (millis() - pcid_tmr > 5000){
    pcid_tmr = millis();
    digitalWrite(RC522_RST, HIGH);
    delay(1);
    digitalWrite(RC522_RST, LOW);
    rfid.PCD_Init();
  }
}

void addFP(){
  if (fpr.total_fingerprints() < MAX_FP){
    uint8_t p, res;
    for (slot = 1; slot <= MAX_FP; slot++){
      if (fpr.permission(slot, &p) == 0x05)
        break; 
    }
    for (uint8_t j = 0; j < 3; j++){
      res = fpr.enroll_fingerprint(slot, 1, (waveshare_fingerprint::EEnrollStage)j);
      if (res == 0x01){
        Serial.write("FPR_ERR");
        state = RUN;
        break;
      }
      else if (res == 0x08){
        Serial.write("FPR_TIMEOUT");
        state = RUN;
        break;
      }
      else if (res == 0x07){
        Serial.write("FPR_EXISTS");
        state = RUN;
      }
      else if (res == 0x00 and j == 2){
        Serial.write("FPR_ADD");
        state = RUN;
      }
    }
  }
  else{
    Serial.write("FPR_MAX");
    state = RUN;
  }
}

void deleteFP(){
  slot = 0;
  fpr.sleep_1_to_N_scan(&slot);
  if (slot > 0 and slot != 0xFFFF){
    fpr.remove(slot);
    Serial.write("FPR_DEL");
    state = RUN; 
  }
  else if (slot == 0xFFFF){
    Serial.write("FPR_NOT_FOUND");
    state = RUN;
  }
}

void changeCode(){
  char key = keypad.getKey();
  if ((key == '*') || pos == (PASSWORD_LENGTH - 1)){
    memset(code, 0, PASSWORD_LENGTH);
    pos = 0;
  }
  else if (key){
    code[pos] = key;
    pos++;
    if (strlen(code) == (PASSWORD_LENGTH - 1)){
      if (strchr(code, '#') == NULL and code[0] != '0'){
        EEPROM.put((MAX_TAGS * 8) + EEPROM_START_ADDR + 2, (uint32_t)strtol(code, NULL, 10));
        Serial.write("PSW_CHANGED");
        state = RUN;
      }
      else{
        Serial.write("PSW_INVALID");
        state = RUN;
      }
    }
  }
}
