#include <Wire.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include "VirtuinoCM.h"
#include <DHT.h>

// === CHÂN KẾT NỐI ===
const int soilPin = A0;
const int rainPin = A1;
const int waterPin = A2;
const int buttonPin = 5;  // Nút bấm điều khiển bơm
const int pumpPin = 8;
const int ledErrorPin = 13;
const int dhtPin = 4;  // Chân kết nối DHT11

SoftwareSerial BTSerial(2, 3);  // RX, TX của HC-05
RTC_DS1307 rtc;
VirtuinoCM virtuino;
DHT dht(dhtPin, DHT11);

#define RELAY_ON LOW
#define RELAY_OFF HIGH

const int soilPctStopThreshold = 55;
const int humidityThreshold = 85;
const int rainThreshold = 600;
const int waterThreshold = 500;

#define V_memory_count 8
float V[V_memory_count];

#define M_memory_count 1
String M[M_memory_count];

bool pumpState = false;
bool manualMode = false;
bool sensorError = false;
bool soilError = false;
bool rainError = false;
bool waterError = false;
bool pumpError = false;
bool dhtError = false;

bool inWateringTime = false;
bool hasWatered = false;

int wateringHour1 = 6;
int wateringMin1 = 0;
int wateringHour2 = 12;
int wateringMin2 = 0;

unsigned long lastDHTReadTime = 0;
const unsigned long DHT_READ_INTERVAL = 2000;
float lastTemperature = 0;
float lastHumidity = 0;

// Khắc phục treo tạm thời: Delay sau khi tắt bơm trước khi đọc DHT
unsigned long lastPumpOffTime = 0;
bool justTurnedPumpOff = false;

const unsigned long debounceDelay = 50;
static bool lastBtn = HIGH;
static unsigned long lastDebounceTime = 0;

void onReceived(char variableType, uint8_t variableIndex, String valueAsText) {
  if (variableType == 'V') {
    float value = valueAsText.toFloat();
    if (variableIndex < V_memory_count) {
      V[variableIndex] = value;

      if (variableIndex == 4) {
        if (!sensorError && (analogRead(waterPin) > waterThreshold)) {
          pumpState = (value == 1);
          digitalWrite(pumpPin, pumpState ? RELAY_ON : RELAY_OFF);
        }
      } else if (variableIndex == 5) {
        manualMode = (value == 1);
        if (!manualMode) {
          pumpState = false;
          digitalWrite(pumpPin, RELAY_OFF);
          V[4] = 0;
        }
      } else if (variableIndex == 6) {
        wateringHour1 = (int)value;
        if (wateringHour1 < 0) wateringHour1 = 0;
        if (wateringHour1 > 23) wateringHour1 = 23;
        V[6] = wateringHour1;
        int h2, m2;
        calcNextWateringTime(wateringHour1, wateringMin1, &h2, &m2);
        wateringHour2 = h2;
        wateringMin2 = m2;
      } else if (variableIndex == 7) {
        wateringMin1 = (int)value;
        if (wateringMin1 < 0) wateringMin1 = 0;
        if (wateringMin1 > 59) wateringMin1 = 59;
        V[7] = wateringMin1;
        int h2, m2;
        calcNextWateringTime(wateringHour1, wateringMin1, &h2, &m2);
        wateringHour2 = h2;
        wateringMin2 = m2;
      }
    }
  }
}

String onRequested(char variableType, uint8_t variableIndex) {
  if (variableType == 'V') {
    if (variableIndex < V_memory_count) return String(V[variableIndex]);
  } else if (variableType == 'M') {
    if (variableIndex < M_memory_count) return M[variableIndex];
  }
  return "";
}

void calcNextWateringTime(int h1, int m1, int* h2, int* m2) {
  int totalMinute = h1 * 60 + m1 + 360;
  *h2 = (totalMinute / 60) % 24;
  *m2 = totalMinute % 60;
}

void setup() {
  Serial.begin(9600);
  BTSerial.begin(38400);
  Wire.begin();
  rtc.begin();
  dht.begin();
  pinMode(pumpPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledErrorPin, OUTPUT);
  digitalWrite(pumpPin, RELAY_OFF);
  digitalWrite(ledErrorPin, LOW);
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  virtuino.begin(onReceived, onRequested, 256);

  V[6] = wateringHour1;
  V[7] = wateringMin1;
  calcNextWateringTime(wateringHour1, wateringMin1, &wateringHour2, &wateringMin2);

  Serial.println("== HỆ THỐNG TƯỚI CÂY TỰ ĐỘNG & THỦ CÔNG ==");
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
  bool waitAfterPump = justTurnedPumpOff && (currentMillis - lastPumpOffTime < 600);

  // Đọc DHT chỉ khi bơm tắt và chờ sau khi tắt bơm 600ms
  if (!pumpState && !waitAfterPump && (currentMillis - lastDHTReadTime >= DHT_READ_INTERVAL)) {
    float newTemperature = dht.readTemperature();
    float newHumidity = dht.readHumidity();

    if (!isnan(newTemperature) && !isnan(newHumidity)) {
      lastTemperature = newTemperature;
      lastHumidity = newHumidity;
      dhtError = false;
    } else {
      dhtError = true;
    }
    lastDHTReadTime = currentMillis;
    justTurnedPumpOff = false;
  }

  V[0] = soilPercent;
  V[1] = lastHumidity;
  V[2] = lastTemperature;
  V[3] = (rainVal < rainThreshold) ? 1 : 0;
  V[4] = pumpState ? 1 : 0;
  V[5] = manualMode ? 1 : 0;
  V[6] = wateringHour1;
  V[7] = wateringMin1;

  if (dhtError) {
    M[0] = "DHT ERROR";
  } else if (rainVal < rainThreshold) {
    M[0] = "Mua";
  } else {
    M[0] = "OK";
  }

  soilError = (soilRaw < 0 || soilRaw > 1023);
  rainError = (rainVal < 0 || rainVal > 1023);
  waterError = (waterVal < 0 || waterVal > 1023);
  sensorError = soilError || rainError || waterError || dhtError;

  digitalWrite(ledErrorPin, sensorError ? HIGH : LOW);

  bool currBtn = digitalRead(buttonPin);
  if (currBtn != lastBtn) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (lastBtn == HIGH && currBtn == LOW) {
      if (!sensorError && (waterVal > waterThreshold)) {
        pumpState = !pumpState;
        digitalWrite(pumpPin, pumpState ? RELAY_ON : RELAY_OFF);
        V[4] = pumpState ? 1 : 0;
        Serial.println(pumpState ? ">> Bơm: BẬT" : ">> Bơm: TẮT");
        BTSerial.println(pumpState ? "PUMP: ON" : "PUMP: OFF");
      }
    }
  }
  lastBtn = currBtn;

  // Tưới tự động
  if (!manualMode) {
    bool timeToWater1 = (hour == wateringHour1 && minute == wateringMin1);
    bool timeToWater2 = (hour == wateringHour2 && minute == wateringMin2);
    inWateringTime = timeToWater1 || timeToWater2;
    bool soilMoist = (soilPercent >= soilPctStopThreshold);  // đất ẩm
    bool airMoist = (lastHumidity >= humidityThreshold);     // không khí ẩm
    bool isRaining = (rainVal < rainThreshold);              // có mưa
    bool waterOK = (waterVal > waterThreshold);              // còn nước

    bool shouldWater = false;
    if (!inWateringTime) {
      Serial.println("Chưa đến giờ tưới.");
    } else if (!waterOK) {
      Serial.println("Hết nước không tưới.");
    } else if (isRaining) {
      Serial.println("Trời mưa không tưới.");
    } else if (soilMoist) {
      Serial.println("Đất ẩm không tưới.");
    } else if (airMoist) {
      Serial.println("Không khí ẩm không tưới.");
    } else if (sensorError) {
      Serial.println("Lỗi cảm biến không tưới.");
    } else {
      // Đúng điều kiện tưới
      shouldWater = true;
      Serial.println("Đến giờ tưới - Đang tưới.");
    }

    if (shouldWater) {
      if (!pumpState) {
        pumpState = true;
        digitalWrite(pumpPin, RELAY_ON);
        V[4] = 1;
        hasWatered = true;
      }
    } else {
      if (pumpState) {
        pumpState = false;
        digitalWrite(pumpPin, RELAY_OFF);
        V[4] = 0;
        lastPumpOffTime = millis();
        justTurnedPumpOff = true;
      }
    }

    if (!inWateringTime && hasWatered) {
      pumpError = (soilPercent < soilPctStopThreshold);
      hasWatered = false;
    }
  }
  while (BTSerial.available()) {
    virtuino.readBuffer = BTSerial.readStringUntil('\n');
    String* response = virtuino.getResponse();
    if (response->length() > 0) {
      BTSerial.println(*response);
    }
  }

  String rainSt = (rainVal < rainThreshold) ? "Yes" : "No";
  String waterSt = (waterVal > waterThreshold) ? "Yes" : "No";
  String pumpSt = pumpState ? "ON" : "OFF";
  String modeSt = manualMode ? "MANUAL" : "AUTO";
  String tempSt = String(lastTemperature, 1) + "°C";
  String humiSt = String(lastHumidity, 1) + "%";

  Serial.print("[" + String(hour) + ":" + String(minute) + "] ");
  Serial.print("Soil: " + String(soilPercent) + "%, ");
  Serial.print("Rain: " + rainSt + ", ");
  Serial.print("Water: " + waterSt + ", ");
  Serial.print("Pump: " + pumpSt + ", ");
  Serial.print("Mode: " + modeSt + ", ");
  Serial.print("Temp: " + tempSt + ", ");
  Serial.print("Humi: " + humiSt + ", ");
  Serial.print("Water Time 1: " + String(wateringHour1) + ":" + String(wateringMin1) + ", ");
  Serial.println("Water Time 2: " + String(wateringHour2) + ":" + String(wateringMin2));

  delay(100);
}
