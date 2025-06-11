#include <Wire.h>
#include <RTClib.h>
#include <Servo.h>

// Pin assignments
const int rainSensorPin = A1;        // Cảm biến mưa
const int moistureSensorPin = A2;    // Cảm biến độ ẩm đất
const int waterLevelPin = A3;        // Cảm biến mực nước (Water Level Sensor)
const int pumpPin = 8;               // Điều khiển máy bơm
const int servoPin = 9;              // Điều khiển servo (Mái che)

// Ngưỡng cảm biến
const int rainThreshold = 600;       // Ngưỡng mưa
const int moistureThreshold = 400;   // Ngưỡng độ ẩm đất
const int waterLevelThreshold = 200; // Ngưỡng mực nước (sẽ điều chỉnh tùy cảm biến)

// Thời gian tưới cây
const int wateringHour1 = 6;        // 6:00 AM
const int wateringMinute1 = 0;      // 0 phút
const int wateringHour2 = 18;       // 6:00 PM
const int wateringMinute2 = 0;      // 0 phút

Servo myServo;
RTC_DS1307 rtc;

int lastPumpState = LOW;            // Trạng thái bơm
int lastServoState = 0;             // Trạng thái servo (mái che)

void setup() {
  Serial.begin(9600);
  pinMode(rainSensorPin, INPUT);
  pinMode(moistureSensorPin, INPUT);
  pinMode(waterLevelPin, INPUT);
  pinMode(pumpPin, OUTPUT);
  pinMode(servoPin, OUTPUT);

  myServo.attach(servoPin);          // Gắn servo cho mái che

  // RTC setup
  Wire.begin();
  rtc.begin();
  if (!rtc.isrunning()) {
    Serial.println("RTC is NOT running, setting time to compile time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

void loop() {
  // Đọc giá trị từ các cảm biến
  int rainValue = analogRead(rainSensorPin);
  int moistureValue = analogRead(moistureSensorPin);
  int waterLevelValue = analogRead(waterLevelPin);
  DateTime now = rtc.now();

  // Log thông tin từ cảm biến và thời gian
  Serial.print("Time: ");
  Serial.print(now.hour());
  Serial.print(":");
  Serial.print(now.minute());
  Serial.print(" | Rain value: ");
  Serial.print(rainValue);
  Serial.print(" | Moisture value: ");
  Serial.print(moistureValue);
  Serial.print(" | Water level value: ");
  Serial.println(waterLevelValue);

  // Kiểm tra điều kiện
  bool raining = rainValue < rainThreshold;  // Trời mưa
  bool moistureLow = moistureValue < moistureThreshold;  // Độ ẩm đất thấp
  bool waterLevelLow = waterLevelValue < waterLevelThreshold;  // Mực nước thấp
  bool wateringNow = isTimeToWater(now);  // Kiểm tra giờ tưới

  // Trường hợp mưa và không tưới
  if (raining) {
    Serial.println("Rain detected, stopping watering.");
    digitalWrite(pumpPin, LOW);  // Tắt bơm
  }
  // Trường hợp độ ẩm đất thấp và không có mưa -> Tưới cây
  else if (wateringNow && moistureLow && !waterLevelLow) {
    digitalWrite(pumpPin, HIGH);  // Bật bơm
    if (lastPumpState != HIGH) {
      Serial.println("Moisture low, watering...");
    }
    lastPumpState = HIGH;
  }
  // Trường hợp độ ẩm đủ cao và không tưới
  else if (moistureValue >= moistureThreshold) {
    digitalWrite(pumpPin, LOW);  // Không tưới nếu độ ẩm đất đủ cao
    if (lastPumpState != LOW) {
      Serial.println("Moisture sufficient, no watering.");
    }
    lastPumpState = LOW;
  }

  // Kiểm tra mực nước trong bồn
  if (waterLevelLow) {
    digitalWrite(pumpPin, LOW);  // Dừng bơm khi hết nước
    Serial.println("Water level low, stopping pump.");
    // Thêm cảnh báo như LED đỏ, còi, hoặc thông báo app ở đây
  }

  // Điều khiển servo cho mái che khi trời mưa và độ ẩm đất đủ cao
  if (raining && moistureValue >= moistureThreshold) {
    if (lastServoState != 1) {
      myServo.write(90);  // Kéo mái che ra
      lastServoState = 1;
      Serial.println("Rain detected, servo activated for cover.");
    }
  } else {
    if (lastServoState != 0) {
      myServo.write(0);  // Đưa mái che về vị trí ban đầu
      lastServoState = 0;
      Serial.println("No rain or moisture level too low, servo returned.");
    }
  }

  delay(1000);  // Đợi 1 giây
}

// Kiểm tra giờ tưới
bool isTimeToWater(DateTime now) {
  return (now.hour() == wateringHour1 && now.minute() == wateringMinute1) ||
         (now.hour() == wateringHour2 && now.minute() == wateringMinute2);
}
