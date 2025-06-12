#include <Wire.h>
#include <RTClib.h>
#include <Servo.h>
#include <DHT.h>
#include <SoftwareSerial.h>

// Pin assignments
const int DHTPIN = 2;              // DHT11 sensor
const int rainSensorPin = A1;      // Cảm biến mưa (Jopto Rain Sensor)
const int moistureSensorPin = A2;  // Cảm biến độ ẩm đất (M9BI-YL69)
const int waterLevelPin = A3;      // Cảm biến mực nước (HW-038)
const int relayPin = 8;            // Relay module CW025 điều khiển máy bơm
const int servoPin = 9;            // Điều khiển servo (Mái che)
const int bluetoothTX = 10;        // Bluetooth module ZS-040
const int bluetoothRX = 11;        // Bluetooth module ZS-040

// Khởi tạo các đối tượng
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
Servo myServo;
RTC_DS1307 rtc;
SoftwareSerial bluetooth(bluetoothTX, bluetoothRX);

// Ngưỡng cảm biến
const int rainThreshold = 600;       // Ngưỡng mưa
const int moistureThreshold = 400;   // Ngưỡng độ ẩm đất
const int waterLevelThreshold = 200; // Ngưỡng mực nước
const float tempThreshold = 35.0;    // Ngưỡng nhiệt độ (độ C)
const float humidityThreshold = 80.0;// Ngưỡng độ ẩm không khí (%)

// Thời gian tưới cây
const int wateringHour1 = 6;        // 6:00 AM
const int wateringMinute1 = 0;
const int wateringHour2 = 18;       // 6:00 PM
const int wateringMinute2 = 0;

// Trạng thái hệ thống
bool manualControl = false;          // Chế độ điều khiển thủ công
int lastPumpState = LOW;
int lastServoState = 0;
String bluetoothData = "";           // Dữ liệu nhận từ Bluetooth

void setup() {
  Serial.begin(9600);
  bluetooth.begin(9600);
  
  // Khởi tạo các chân
  pinMode(rainSensorPin, INPUT);
  pinMode(moistureSensorPin, INPUT);
  pinMode(waterLevelPin, INPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(servoPin, OUTPUT);
  
  // Khởi tạo các module
  dht.begin();
  myServo.attach(servoPin);
  
  // RTC setup
  Wire.begin();
  rtc.begin();
  if (!rtc.isrunning()) {
    Serial.println("RTC is NOT running, setting time to compile time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  // Khởi tạo trạng thái
  digitalWrite(relayPin, LOW);
  myServo.write(0);
  
  Serial.println("System initialized!");
}

void loop() {
  // Đọc dữ liệu từ các cảm biến
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int rainValue = analogRead(rainSensorPin);
  int moistureValue = analogRead(moistureSensorPin);
  int waterLevelValue = analogRead(waterLevelPin);
  DateTime now = rtc.now();
  
  // Kiểm tra lỗi cảm biến DHT
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    stopSystem();
    return;
  }
  
  // Đọc dữ liệu Bluetooth
  if (bluetooth.available()) {
    char c = bluetooth.read();
    if (c == '\n') {
      processBluetoothCommand(bluetoothData);
      bluetoothData = "";
    } else {
      bluetoothData += c;
    }
  }
  
  // Log thông tin
  logSensorData(now, temperature, humidity, rainValue, moistureValue, waterLevelValue);
  
  // Kiểm tra điều kiện
  bool raining = rainValue < rainThreshold;
  bool moistureLow = moistureValue < moistureThreshold;
  bool waterLevelLow = waterLevelValue < waterLevelThreshold;
  bool wateringTime = isTimeToWater(now);
  
  // Xử lý logic tưới cây
  if (!manualControl) {
    if (waterLevelLow) {
      stopSystem();
      sendAlert("Water level low!");
    } else if (raining) {
      stopPump();
      Serial.println("Rain detected, stopping irrigation.");
    } else if ((wateringTime || moistureLow) && !waterLevelLow) {
      startPump();
    } else if (moistureValue >= moistureThreshold) {
      stopPump();
    }
    
    // Điều khiển mái che
    controlCover(raining, moistureValue);
  }
  
  // Gửi dữ liệu qua Bluetooth
  sendDataToApp(temperature, humidity, rainValue, moistureValue, waterLevelValue);
  
  delay(1000);
}
// Xử lý lệnh từ Bluetooth
void processBluetoothCommand(String command) {
  if (command == "MANUAL") {
    manualControl = true;
    Serial.println("Manual control activated");
  } else if (command == "AUTO") {
    manualControl = false;
    Serial.println("Automatic control activated");
  } else if (command == "PUMP_ON" && manualControl) {
    startPump();
  } else if (command == "PUMP_OFF" && manualControl) {
    stopPump();
  }
}

// Bật bơm
void startPump() {
  if (lastPumpState != HIGH) {
    digitalWrite(relayPin, HIGH);
    lastPumpState = HIGH;
    Serial.println("Pump started");
  }
}

// Tắt bơm
void stopPump() {
  if (lastPumpState != LOW) {
    digitalWrite(relayPin, LOW);
    lastPumpState = LOW;
    Serial.println("Pump stopped");
  }
}

  // Điều khiển mái che 
void controlCover(bool isRaining, int moistureValue) {
  if (isRaining && moistureValue >= moistureThreshold) {
    if (lastServoState != 1) {
      myServo.write(90);
      lastServoState = 1;
      Serial.println("Cover activated");
    }
  } else {
    if (lastServoState != 0) {
      myServo.write(0);
      lastServoState = 0;
      Serial.println("Cover deactivated");
    }
  }
}

// Tắt hệ thống
void stopSystem() {
  digitalWrite(relayPin, LOW);
  myServo.write(0);
  lastPumpState = LOW;
  lastServoState = 0;
}

// Gửi thông báo qua Bluetooth
void sendAlert(String message) {
  Serial.println("ALERT: " + message);
  bluetooth.println("ALERT:" + message);
}

// Ghi log dữ liệu cảm biến
void logSensorData(DateTime now, float temp, float humidity, int rain, int moisture, int waterLevel) {
  String data = String(now.hour()) + ":" + String(now.minute()) + " | ";
  data += "Temp: " + String(temp) + "C | ";
  data += "Humidity: " + String(humidity) + "% | ";
  data += "Rain: " + String(rain) + " | ";
  data += "Moisture: " + String(moisture) + " | ";
  data += "Water: " + String(waterLevel);
  Serial.println(data);
}

// Gửi dữ liệu qua Bluetooth
void sendDataToApp(float temp, float humidity, int rain, int moisture, int waterLevel) {
  String data = "DATA:";
  data += String(temp) + ",";
  data += String(humidity) + ",";
  data += String(rain) + ",";
  data += String(moisture) + ",";
  data += String(waterLevel) + ",";
  data += String(lastPumpState) + ",";
  data += String(lastServoState);
  bluetooth.println(data);
}

// Kiểm tra thời gian tưới cây
bool isTimeToWater(DateTime now) {
  return (now.hour() == wateringHour1 && now.minute() == wateringMinute1) ||
         (now.hour() == wateringHour2 && now.minute() == wateringMinute2);
}
