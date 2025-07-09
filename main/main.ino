#include <Wire.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// === PIN CONNECTIONS ===
const int soilPin = A0;
const int rainPin = A1;
const int waterPin = A2;
const int ds18b20Pin = 7;
const int pumpPin = 8;

SoftwareSerial BTSerial(2, 3);  // RX, TX for HC-05
RTC_DS1307 rtc;

// DS18B20 setup
OneWire oneWire(ds18b20Pin);
DallasTemperature sensors(&oneWire);

#define RELAY_ON LOW
#define RELAY_OFF HIGH

const int soilPctStopThreshold = 55;
const int rainThreshold = 600;
const int waterThreshold = 300;

bool pumpState = false;
bool manualMode = false;
bool sensorError = false;
bool soilError = false;
bool rainError = false;
bool waterError = false;
bool set2Overridden = false;

int wateringHour1 = 6;
int wateringMin1 = 0;
int wateringHour2 = 12;
int wateringMin2 = 0;

float lastTemperature = 0;
unsigned long lastTempReadTime = 0;
const unsigned long TEMP_READ_INTERVAL = 1000;

void calcNextWateringTime(int h1, int m1, int* h2, int* m2) {
  int totalMinute = h1 * 60 + m1 + 360;
  *h2 = (totalMinute / 60) % 24;
  *m2 = totalMinute % 60;
}

float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

unsigned long lastSendTime = 0;
String lastSentData = "";

bool wateringInProgress = false;
unsigned long wateringStartMillis = 0;
const unsigned long WATERING_DURATION = 3UL * 60UL * 1000UL;

void setup() {
  Serial.begin(9600);
  BTSerial.begin(38400);
  Wire.begin();
  rtc.begin();
  sensors.begin();

  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, RELAY_OFF);

  // CHỈ BỎ COMMENT DÒNG NÀY NẾU MUỐN SET GIỜ LẠI CHO RTC,
  // SAU ĐÓ PHẢI COMMENT LẠI NGAY ĐỂ GIỜ KHÔNG BỊ RESET LIÊN TỤC
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  calcNextWateringTime(wateringHour1, wateringMin1, &wateringHour2, &wateringMin2);
  set2Overridden = false;

  Serial.println("== AUTOMATIC PLANT WATERING SYSTEM VIA BLUETOOTH ==");
  BTSerial.println("SYSTEM: START");
}

void loop() {
  DateTime now = rtc.now();
  int hour = now.hour();
  int minute = now.minute();

  int soilRaw = analogRead(soilPin);
  int rainVal = analogRead(rainPin);
  int waterVal = analogRead(waterPin);
  int soilPercent = map(soilRaw, 0, 1023, 100, 0);

  unsigned long currentMillis = millis();
  if (currentMillis - lastTempReadTime >= TEMP_READ_INTERVAL) {
    lastTemperature = readTemperature();
    lastTempReadTime = currentMillis;
  }

  soilError = (soilRaw < 0 || soilRaw > 1023);
  rainError = (rainVal < 0 || rainVal > 1023);
  waterError = (waterVal < 0 || waterVal > 1023);
  sensorError = soilError || rainError || waterError;

  // --- Bluetooth Commands ---
  if (BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    // Clean up received cmd: remove CR, LF, and trim
    cmd.replace("\r", "");
    cmd.replace("\n", "");
    cmd.trim();
    cmd.toLowerCase();
    if (cmd.length() == 0) return;

    if (cmd == "on") {
      if (!sensorError && (waterVal > waterThreshold)) {
        pumpState = true;
        manualMode = true;
        digitalWrite(pumpPin, RELAY_ON);
        BTSerial.println("PUMP: ON (Manual)");
      } else {
        BTSerial.println("❌ Không thể bật bơm (manual): Lỗi cảm biến hoặc hết nước");
      }
    } else if (cmd == "off") {
      pumpState = false;
      digitalWrite(pumpPin, RELAY_OFF);
      BTSerial.println("PUMP: OFF");
    } else if (cmd == "manual") {
      manualMode = true;
      BTSerial.println("MODE: MANUAL");
    } else if (cmd == "auto") {
      manualMode = false;
      pumpState = false;
      digitalWrite(pumpPin, RELAY_OFF);
      BTSerial.println("MODE: AUTO");
    } else if (cmd.startsWith("set1 ")) {
      int sep = cmd.indexOf(':', 5);
      if (sep > 0) {
        int h = cmd.substring(5, sep).toInt();
        int m = cmd.substring(sep + 1).toInt();
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
          wateringHour1 = h;
          wateringMin1 = m;
          if (!set2Overridden) {
            calcNextWateringTime(h, m, &wateringHour2, &wateringMin2);
            BTSerial.println("Auto-set Watering 2: " + String(wateringHour2) + ":" + (wateringMin2 < 10 ? "0" : "") + String(wateringMin2));
          }
          BTSerial.println("Set Watering 1: " + String(h) + ":" + (m < 10 ? "0" : "") + String(m));
        } else {
          BTSerial.println("ERR: Invalid time!");
        }
      } else {
        BTSerial.println("ERR: Format set1 hh:mm");
      }
    } else if (cmd.startsWith("set2 ")) {
      int sep = cmd.indexOf(':', 5);
      if (sep > 0) {
        int h = cmd.substring(5, sep).toInt();
        int m = cmd.substring(sep + 1).toInt();
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
          wateringHour2 = h;
          wateringMin2 = m;
          set2Overridden = true;
          BTSerial.println("Set Watering 2: " + String(h) + ":" + (m < 10 ? "0" : "") + String(m));
        } else {
          BTSerial.println("ERR: Invalid time!");
        }
      } else {
        BTSerial.println("ERR: Format set2 hh:mm");
      }
    } else {
      BTSerial.println("CMD? Unknown: " + cmd);
    }
  }

  // --- Automatic watering logic with time & temp protection (the new logic) ---

  // Flags for watering time
  bool isWateringTime1 = (hour == wateringHour1 && minute == wateringMin1);
  bool isWateringTime2 = (hour == wateringHour2 && minute == wateringMin2);
  bool isWateringTime = isWateringTime1 || isWateringTime2;

  bool isHighTemp = (lastTemperature >= 40.0);
  bool isIn11to15 = (hour >= 11 && hour < 15);

  // Watering permission by temp & time logic
  bool allowWateringByTempTime = false;
  if (isHighTemp && isIn11to15) {
    allowWateringByTempTime = false;
  } else if (isWateringTime) {
    allowWateringByTempTime = true;
  } else {
    allowWateringByTempTime = false;
  }

  // Other conditions
  bool soilDry = (soilPercent < soilPctStopThreshold);
  bool isRaining = (rainVal < rainThreshold);
  bool waterOK = (waterVal > waterThreshold);

  bool allowWatering = (soilDry && !isRaining && waterOK && !sensorError && allowWateringByTempTime);

  if (!manualMode) {
    // Start watering window
    if (isWateringTime && !wateringInProgress) {
      wateringInProgress = true;
      wateringStartMillis = millis();
    }

    if (wateringInProgress) {
      if (millis() - wateringStartMillis < WATERING_DURATION) {
        if (allowWatering) {
          if (!pumpState) {
            pumpState = true;
            digitalWrite(pumpPin, RELAY_ON);
            Serial.println("🟢 Đang tưới tự động vào lúc " + String(hour) + ":" + (minute < 10 ? "0" : "") + String(minute));
            BTSerial.println("🟢 Đang tưới tự động vào lúc " + String(hour) + ":" + (minute < 10 ? "0" : "") + String(minute));
          }
        } else {
          if (pumpState) {
            pumpState = false;
            digitalWrite(pumpPin, RELAY_OFF);
          }
          // Lý do không tưới
          if (!waterOK) {
            BTSerial.println("❌ Không tưới: Mực nước thấp");
            Serial.println("❌ Không tưới: Mực nước thấp");
          } else if (!soilDry) {
            BTSerial.println("❌ Không tưới: Độ ẩm đất đủ");
            Serial.println("❌ Không tưới: Độ ẩm đất đủ");
          } else if (isRaining) {
            BTSerial.println("❌ Không tưới: Trời đang mưa");
            Serial.println("❌ Không tưới: Trời đang mưa");
          } else if (!allowWateringByTempTime) {
            if (isHighTemp && isIn11to15) {
              BTSerial.println("❌ Không tưới: Nhiệt độ cao trong 11h-15h");
              Serial.println("❌ Không tưới: Nhiệt độ cao trong 11h-15h");
            } else if (isHighTemp && !isWateringTime) {
              BTSerial.println("❌ Không tưới: Nhiệt độ cao ngoài 11h-15h nhưng không trong giờ tưới");
              Serial.println("❌ Không tưới: Nhiệt độ cao ngoài 11h-15h nhưng không trong giờ tưới");
            } else if (!isHighTemp && !isWateringTime) {
              BTSerial.println("❌ Không tưới: Không trong giờ tưới");
              Serial.println("❌ Không tưới: Không trong giờ tưới");
            }
          }
        }
      } else {
        pumpState = false;
        digitalWrite(pumpPin, RELAY_OFF);
        wateringInProgress = false;
      }
    }
  }

  // Bảo vệ bơm khi manual mà hết nước hoặc lỗi cảm biến
  if (manualMode && pumpState) {
    if (sensorError || waterVal <= waterThreshold) {
      pumpState = false;
      digitalWrite(pumpPin, RELAY_OFF);
      Serial.println("❌ Tắt bơm: Lỗi cảm biến hoặc hết nước (manual)");
      BTSerial.println("❌ Tắt bơm: Lỗi cảm biến hoặc hết nước (manual)");
    }
  }

  // --- Report ---
  String rainSt = (rainVal < rainThreshold) ? "Yes" : "No";
  String waterSt = (waterVal > waterThreshold) ? "Yes" : "No";
  String pumpSt = pumpState ? "ON" : "OFF";
  String modeSt = manualMode ? "MANUAL" : "AUTO";
  String tempSt = String(lastTemperature, 1) + "°C";

  Serial.print("[" + String(hour) + ":" + String(minute) + "] ");
  Serial.print("Soil: " + String(soilPercent) + "%, ");
  Serial.print("Rain: " + rainSt + ", ");
  Serial.print("Water: " + String(waterVal) + ", ");
  Serial.print("Pump: " + pumpSt + ", ");
  Serial.print("Mode: " + modeSt + ", ");
  Serial.print("Temp: " + tempSt + ", ");
  Serial.print("Water Time 1: " + String(wateringHour1) + ":" + String(wateringMin1) + ", ");
  Serial.println("Water Time 2: " + String(wateringHour2) + ":" + String(wateringMin2));

  String btData = "[" + String(hour) + ":" + String(minute) + "] " +
                  "Soil:" + String(soilPercent) + " " +
                  "Rain:" + rainSt + " " +
                  "Water:" + waterSt + " " +
                  "Temp:" + String(lastTemperature, 1) + " " +
                  "Pump:" + String(pumpState ? 1 : 0) + " " +
                  "Mode:" + (manualMode ? "M" : "A");

  if (btData != lastSentData) {
    BTSerial.println(btData);
    lastSentData = btData;
  }

  delay(100);
}