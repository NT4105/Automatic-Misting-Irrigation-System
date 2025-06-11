# Automatic Misting Irrigation System

## Overview

This project is an automated irrigation system that uses various sensors to monitor environmental conditions and control water misting based on predefined parameters. The system can be controlled manually via Bluetooth using the Virtuino IoT mobile application.

## Features

- Real-time environmental humidity monitoring
- Automatic pump activation based on sensor conditions
- Rain detection for smart irrigation control
- Manual control through Bluetooth interface
- Real-time clock functionality for scheduled operations
- Mobile app control using Virtuino IoT

## Hardware Components

- Arduino Uno microcontroller
- 5V water pump
- Temperature sensor
- Rain Water Sensor
- Soil Moisture Sensor
- Bluetooth module ZS-040
- DS1307 Real-Time Clock module

## Software Requirements

- Arduino IDE
- Virtuino IoT mobile application

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
   - Bluetooth connectivity via ZS-040 module
   - Remote system control through Virtuino IoT app
   - Real-time sensor data monitoring
   - Manual pump activation/deactivation

## Setup and Configuration

1. Install the Arduino IDE
2. Download and install Virtuino IoT app on your mobile device
3. Connect the hardware components according to the circuit diagram
4. Upload the provided Arduino code
5. Pair your mobile device with the ZS-040 Bluetooth module
6. Configure the Virtuino IoT app settings

## Contributing

Feel free to contribute to this project by submitting issues or pull requests.

## License

This project is open-source and available under the MIT License.
