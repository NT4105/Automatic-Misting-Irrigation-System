# Automatic Misting Irrigation System

## Overview

This project is an automated irrigation system that uses various sensors to monitor environmental conditions and control water misting based on predefined parameters. The system can be controlled manually via Bluetooth using the Serial Bluetooth Terminal mobile application.

## Team Members

- To Thanh Dat (Leader)
- Nguyen Tan Kim Hao
- Nguyen Minh Tu

## Features

- Real-time environmental humidity monitoring
- Automatic pump activation based on sensor conditions
- Rain detection for smart irrigation control
- Manual control through Bluetooth interface
- Real-time clock functionality for scheduled operations
- Mobile app control using Serial Bluetooth Terminal (avaiable for Android os)

## Hardware Components

- Arduino Board UNO R3
- DC 3V-5V Mini Submersible Water Pump
- M9BI-YL69 Soil Moisture Sensor
- Jopto Rain Sensor
- DS18b20 Temperature Sensor
- HW-038 Water Level Sensor
- CW025 Relay Module
- DS1307 Real-Time Clock Module
- ZS-040 Bluetooth Module (HC-05 has button)
- Water pipes and tubing
- Jumper wires
- Water container/tank
- Plant pot
- Breadboard

## Software Requirements

- Arduino IDE
- Serial Bluetooth Terminal mobile application (preferred over Blynk due to better HC-05/HC06 support)
- Libraries: RTClib, DallasTemperature

## System Operation

1. **Environmental Monitoring**

   - Continuous monitoring of soil moisture levels
   - Temperature tracking
   - Rain detection
   - Real-time clock synchronization

2. **Automatic Control**

   - Automated pump activation when moisture levels are low
   - System deactivation during rain detection
   - Scheduled operations using real-time clock

3. **Manual Control**
   - Bluetooth connectivity via HC-05 module
   - Remote system control through Serial Bluetooth Terminal app
   - Real-time sensor data monitoring
   - Manual pump activation/deactivation

## Setup and Configuration

1. Install the Arduino IDE
2. Download and install Serial Bluetooth Terminal app on your mobile device
3. Connect the hardware components according to the circuit diagram
4. Upload the provided Arduino code
5. Pair your mobile device with the HC-05 Bluetooth module
6. Configure the Serial Bluetooth Terminal app settings

## Contributing

Feel free to contribute to this project by submitting issues or pull requests.

## License

This project is open-source and available under the MIT License.
