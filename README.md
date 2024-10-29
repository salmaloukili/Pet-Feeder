# Smart Pet Feeder

A smart, automated pet feeder designed to ensure pets are fed, entertained, and monitored remotely. This system is powered by an ESP32 master node with STM8 slave nodes for handling interactions, feeding, and communication via a web interface.

## Table of Contents
- [Features](#features)
- [System Architecture](#system-architecture)
- [Hardware Components](#hardware-components)
- [Software Components](#software-components)
- [Setup and Usage](#setup-and-usage)


---

## Features
- **Automated Feeding:** Dispenses food based on set schedules or remote commands.
- **Pet Interaction:** Includes motion detection and audio playback to engage pets.
- **Remote Control:** Web interface enables monitoring and interaction from any device.
- **Low Power Consumption:** Efficient wireless communication via nRF24 for power savings.

## System Architecture
The system comprises:
1. **Master Node (ESP32):** Controls the web interface and manages communication with slave nodes.
2. **Slave Nodes (STM8):** Handle feeding mechanics, motion detection, and audio playback.
3. **Wireless Communication:** nRF24 module for robust, low-power communication between nodes.

## Hardware Components
- **ESP32-S3**: For web server and master node functions.
- **STM8S105K4**: Controls slave nodes for feeding and interaction.
- **nRF24L01**: Wireless communication module.
- **Motion Sensor (HC-SR501)**: Detects pet presence.
- **Servo Motor (MG996R)**: Operates food dispenser.
- **Load Cell (HX711)**: Measures food weight.
- **DFPlayer Mini MP3 Module**: Plays audio for pet engagement.
- **Power Supply**: USB-powered with 5V-3.3V step-down regulator.

## Software Components
- **ESP32 Web Server**: Provides an interface for users to monitor and control the feeder.
- **nRF24 Communication Protocol**: Enables low-power data exchange between nodes.
- **FreeRTOS on ESP32**: Manages concurrent tasks for seamless operation.

## Setup and Usage
1. **Hardware Assembly**:
   - Connect the ESP32 and STM8 modules with the required sensors and actuators as per the system diagram.
   - Install the nRF24 module for wireless communication.

2. **Software Setup**:
   - Flash the ESP32 and STM8 with the respective firmware.
   - Connect to the web server through the ESP32’s Wi-Fi network for initial configuration.

3. **Usage**:
   - Use the web interface to set feeding schedules, enable interaction features, and monitor system logs.


This project was developed as part of the Electronics and ICT Engineering program at KU Leuven, Group T Leuven Campus.
