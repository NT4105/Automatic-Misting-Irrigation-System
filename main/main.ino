#include <Wire.h>
#include <RTClib.h>
#include <SoftwareSerial.h>

// === PIN CONNECTIONS ===
const int soilPin = A0;
const int rainPin = A1;
const int waterPin = A2;
const int tempPin = A3;   // LM35
const int pumpPin = 8;

SoftwareSerial BTSerial(2, 3);  // RX, TX for HC-05
RTC_DS1307 rtc;

#define RELAY_ON LOW
#define RELAY_OFF HIGH

const int soilPctStopThreshold = 55;
const int rainThreshold = 600;
const int waterThreshold = 400;
const float tempThreshold = 30.0;  // °C

bool pumpState = false;
bool manualMode = false;
bool sensorError = false;
bool soilError = false;
bool rainError = false;
bool waterError = false;
bool pumpError = false;
bool set2Overridden = false;

int wateringHour1 = 6;
int wateringMin1 = 0;
int wateringHour2 = 12;
int wateringMin2 = 0;

// Variables for LM35
float lastTemperature = 0;
unsigned long lastTempReadTime = 0;
const unsigned long TEMP_READ_INTERVAL = 1000;  // 1 second

#define NUM_SAMPLES 10
int tempReadings[NUM_SAMPLES];

int compare(const void* a, const void* b) {
  return (*(int*)a - *(int*)b);
}

void calcNextWateringTime(int h1, int m1, int* h2, int* m2) {
  int totalMinute = h1 * 60 + m1 + 360;
  *h2 = (totalMinute / 60) % 24;
  *m2 = totalMinute % 60;
}

float readTemperature() {
  for (int i = 0; i < NUM_SAMPLES; i++) {
    tempReadings[i] = analogRead(tempPin);
    delay(2);
  }
  qsort(tempReadings, NUM_SAMPLES, sizeof(int), compare);

  float sum = 0;
  int valid = 0;
  for (int i = 2; i < NUM_SAMPLES - 2; i++) {
    float voltage = tempReadings[i] * 4.8828125;  // 5000 / 1024
    float tempC = voltage / 10.0;
    if (tempC >= 20 && tempC <= 35) {
      sum += tempC;
      valid++;
    }
  }
  return (valid > 0) ? (sum / valid) : 25.0;
}

unsigned long lastSendTime = 0;
String lastSentData = "";

// --- Watering control variables ---
bool wateringInProgress = false;
unsigned long wateringStartMillis = 0;
int windowStartHour = -1, windowStartMinute = -1;
const unsigned long WATERING_DURATION = 3UL * 60UL * 1000UL; // 3 phút

void setup() {
  Serial.begin(9600);
  BTSerial.begin(38400);
  Wire.begin();
  rtc.begin();

  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, RELAY_OFF);

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

  // --- Bluetooth Control ---
  if (BTSerial.available()) {
    String cmd = BTSerial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "on") {
      if (!sensorError && (waterVal > waterThreshold)) {
        pumpState = true;
        manualMode = true;
        digitalWrite(pumpPin, RELAY_ON);
        BTSerial.println("PUMP: ON (Manual)");
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

  // --- Automatic watering logic with window ---
  if (!manualMode) {
    bool timeToWater1 = (hour == wateringHour1 && minute == wateringMin1);
    bool timeToWater2 = (hour == wateringHour2 && minute == wateringMin2);

    bool soilDry = (soilPercent < soilPctStopThreshold);
    bool isRaining = (rainVal < rainThreshold);
    bool waterOK = (waterVal > waterThreshold);

    // Start window when at watering time
    if ((timeToWater1 || timeToWater2) && !wateringInProgress) {
      wateringInProgress = true;
      wateringStartMillis = millis();
    }

    if (wateringInProgress) {
      if (millis() - wateringStartMillis < WATERING_DURATION) {
        if (soilDry && !isRaining && waterOK && !sensorError) {
          if (!pumpState) {
            pumpState = true;
            digitalWrite(pumpPin, RELAY_ON);
          }
        } else {
          if (pumpState) {
            pumpState = false;
            digitalWrite(pumpPin, RELAY_OFF);
          }
        }
      } else {
        pumpState = false;
        digitalWrite(pumpPin, RELAY_OFF);
        wateringInProgress = false;
      }
    }
  }

  // --- Serial và Bluetooth report ---
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