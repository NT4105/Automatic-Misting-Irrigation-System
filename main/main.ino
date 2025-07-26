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
const int waterThreshold = 200;

bool pumpState = false;
bool manualMode = false;
bool sensorError = false;
bool soilError = false;
bool rainError = false;
bool waterError = false;

int wateringHour1 = 6;
int wateringMin1 = 0;
int wateringHour2 = 12;
int wateringMin2 = 0;

float lastTemperature = 0;
unsigned long lastTempReadTime = 0;
const unsigned long TEMP_READ_INTERVAL = 1000;

// --- Store last sent values ---
int lastSentSoil = -1000;
int lastSentWater = -1000;
String lastSentRainSt = "";
String lastSentWaterSt = "";
String lastSentModeSt = "";
String lastSentPumpSt = "";

float lastSentTemp = -1000;

// Store sent watering times
int lastSentWateringHour1 = -1, lastSentWateringMin1 = -1;
int lastSentWateringHour2 = -1, lastSentWateringMin2 = -1;

// Auto watering state
bool wateringInProgress = false;
unsigned long wateringStartMillis = 0;
const unsigned long WATERING_DURATION = 3UL * 60UL * 1000UL;
bool userForcePumpOff = false;

// State flags and trackers
bool notifiedEndWatering = false;
bool notifiedUserStop = false;
bool skipCurrentWatering = false;
int lastWateredHour = -1;
int lastWateredMinute = -1;

float readTemperature() {
  unsigned long start = millis();
  sensors.requestTemperatures();
  while (!sensors.isConversionComplete()) {
    if (millis() - start > 200) break;
    delay(1);
  }
  float t = sensors.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C || t < -50 || t > 100) t = lastTemperature;
  return t;
}

void setup() {
  Serial.begin(9600);
  BTSerial.begin(38400);
  Wire.begin();
  rtc.begin();
  sensors.begin();

  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, RELAY_OFF);

  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

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
  int soilPercent = map(soilRaw, 990, 634, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

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
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.length() == 0) {
      // do nothing
    }
    else if (cmd == "on") {
      if (!sensorError && (waterVal > waterThreshold)) {
        pumpState = true;
        manualMode = true;
        digitalWrite(pumpPin, RELAY_ON);
        BTSerial.println("PUMP: ON (Manual)");
        userForcePumpOff = false;
      } else {
        BTSerial.println("❌ Không thể bật bơm: Mực nước thấp!");
      }
    } else if (cmd == "off") {
      pumpState = false;
      digitalWrite(pumpPin, RELAY_OFF);
      userForcePumpOff = true;
      skipCurrentWatering = true;
      wateringInProgress = false;
      BTSerial.println("PUMP: OFF");
    } else if (cmd == "manual") {
      manualMode = true;
      BTSerial.println("MODE: MANUAL");
      userForcePumpOff = false;
      notifiedUserStop = false;
      skipCurrentWatering = false;
      notifiedEndWatering = false;
    } else if (cmd == "auto") {
      manualMode = false;
      pumpState = false;
      digitalWrite(pumpPin, RELAY_OFF);
      BTSerial.println("MODE: AUTO");
      notifiedUserStop = false;
      notifiedEndWatering = false;
      // Do not reset skipCurrentWatering here, keep as is to avoid watering again until next minute
    } else if (cmd.startsWith("set1 ")) {
      int sep = cmd.indexOf(':', 5);
      if (sep > 0) {
        int h = cmd.substring(5, sep).toInt();
        int m = cmd.substring(sep + 1).toInt();
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
          wateringHour1 = h;
          wateringMin1 = m;
          userForcePumpOff = false;
          notifiedUserStop = false;
          skipCurrentWatering = false;
          notifiedEndWatering = false;
          lastWateredHour = -1;
          lastWateredMinute = -1;
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
          userForcePumpOff = false;
          notifiedUserStop = false;
          skipCurrentWatering = false;
          notifiedEndWatering = false;
          lastWateredHour = -1;
          lastWateredMinute = -1;
          BTSerial.println("Set Watering 2: " + String(h) + ":" + (m < 10 ? "0" : "") + String(m));
        } else {
          BTSerial.println("ERR: Invalid time!");
        }
      } else {
        BTSerial.println("ERR: Format set2 hh:mm");
      }
    }
  }

  // --- Manual mode: auto stop when out of water or when raining ---
  if (manualMode && pumpState) {
    if (waterVal <= waterThreshold) {
      pumpState = false;
      digitalWrite(pumpPin, RELAY_OFF);
      Serial.println("❌ Dừng bơm manual: Mực nước thấp!");
      BTSerial.println("❌ Dừng bơm manual: Mực nước thấp!");
    } else if (rainVal < rainThreshold) {
      pumpState = false;
      digitalWrite(pumpPin, RELAY_OFF);
      Serial.println("❌ Dừng bơm manual: Trời đang mưa!");
      BTSerial.println("❌ Dừng bơm manual: Trời đang mưa!");
    }
  }

  // --- Automatic watering logic with temperature protection ---
  if (!manualMode) {
    bool timeToWater1 = (hour == wateringHour1 && minute == wateringMin1);
    bool timeToWater2 = (hour == wateringHour2 && minute == wateringMin2);

    bool soilDry = (soilPercent < soilPctStopThreshold);
    bool isRaining = (rainVal < rainThreshold);
    bool waterOK = (waterVal > waterThreshold);
    float temp = lastTemperature;

    bool tempTooHotTime = (temp >= 40.0 && hour >= 11 && hour < 15);
    bool tempNormal = (temp < 40.0);
    bool tempSafeTime = (temp >= 40.0 && !tempTooHotTime);

    bool allowWatering = (soilDry && !isRaining && waterOK && !sensorError &&
                      (tempNormal || tempSafeTime));

    // --- Only water once per minute, and only if not skipping current watering ---
    if ((timeToWater1 || timeToWater2) && !wateringInProgress && (hour != lastWateredHour || minute != lastWateredMinute) && !skipCurrentWatering) {
      wateringInProgress = true;
      wateringStartMillis = millis();
      userForcePumpOff = false;
      notifiedUserStop = false;
      skipCurrentWatering = false;
      notifiedEndWatering = false;
      lastWateredHour = hour;
      lastWateredMinute = minute;
    }

    if (wateringInProgress) {
      if (millis() - wateringStartMillis < WATERING_DURATION && !skipCurrentWatering) {
        if (allowWatering && !userForcePumpOff) {
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
          if (userForcePumpOff && !notifiedUserStop) {
            Serial.println("❌ Không tưới: User đã tắt bơm!");
            BTSerial.println("❌ Không tưới: User đã tắt bơm!");
            notifiedUserStop = true;
            skipCurrentWatering = true;
            wateringInProgress = false;
          }
          else if (tempTooHotTime) {
            Serial.println("❌ Không tưới: Nhiệt độ cao và đang trong giờ dễ sốc nhiệt (11h–15h)");
            BTSerial.println("❌ Không tưới: Nhiệt độ cao và đang trong giờ dễ sốc nhiệt (11h–15h)");
          } else if (!soilDry) {
            Serial.println("❌ Không tưới: Độ ẩm đất đủ");
            BTSerial.println("❌ Không tưới: Độ ẩm đất đủ");
          } else if (isRaining) {
            Serial.println("❌ Không tưới: Trời đang mưa");
            BTSerial.println("❌ Không tưới: Trời đang mưa");
          } else if (!waterOK) {
            Serial.println("❌ Không tưới: Mực nước thấp");
            BTSerial.println("❌ Không tưới: Mực nước thấp");
          }
        }
      } else {
        pumpState = false;
        digitalWrite(pumpPin, RELAY_OFF);
        wateringInProgress = false;
        if (!notifiedEndWatering) {
          Serial.println("⏹️ Đã kết thúc chu kỳ tưới tự động.");
          BTSerial.println("⏹️ Đã kết thúc chu kỳ tưới tự động.");
          notifiedEndWatering = true;
        }
      }
    }

    // --- If the minute has passed, allow watering for the next cycle ---
    if ((hour != lastWateredHour || minute != lastWateredMinute)) {
      skipCurrentWatering = false;
      userForcePumpOff = false;
      notifiedUserStop = false;
      notifiedEndWatering = false;
    }
  }

  // --- Handle data sending conditions ---
  String rainSt = (rainVal < rainThreshold) ? "Yes" : "No";
  String waterSt = (waterVal > waterThreshold) ? "Yes" : "No";
  String pumpSt = pumpState ? "ON" : "OFF";
  String modeSt = manualMode ? "MANUAL" : "AUTO";
  String tempSt = String(lastTemperature, 1) + "°C";

  bool sendFlag = false;

  if (abs(soilPercent - lastSentSoil) >= 10) {
    sendFlag = true;
    lastSentSoil = soilPercent;
  }
  if (abs(waterVal - lastSentWater) >= 20) {
    sendFlag = true;
    lastSentWater = waterVal;
  }
  if (rainSt != lastSentRainSt) {
    sendFlag = true;
    lastSentRainSt = rainSt;
  }
  if (waterSt != lastSentWaterSt) {
    sendFlag = true;
    lastSentWaterSt = waterSt;
  }
  if (modeSt != lastSentModeSt) {
    sendFlag = true;
    lastSentModeSt = modeSt;
  }
  if (pumpSt != lastSentPumpSt) {
    sendFlag = true;
    lastSentPumpSt = pumpSt;
  }
  if (abs(lastTemperature - lastSentTemp) >= 1.0) {
    sendFlag = true;
    lastSentTemp = lastTemperature;
  }
  if (wateringHour1 != lastSentWateringHour1 || wateringMin1 != lastSentWateringMin1 ||
      wateringHour2 != lastSentWateringHour2 || wateringMin2 != lastSentWateringMin2) {
    sendFlag = true;
    lastSentWateringHour1 = wateringHour1;
    lastSentWateringMin1 = wateringMin1;
    lastSentWateringHour2 = wateringHour2;
    lastSentWateringMin2 = wateringMin2;
  }

  if (sendFlag) {
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

    BTSerial.println(btData);
  }

  delay(100);
}