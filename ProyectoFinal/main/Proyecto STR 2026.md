## **STR 2026: Automated Environmental Control System** 

**Project Overview:** This project is an IoT-enabled environmental control system designed to manage ventilation, ambient lighting, and automated curtains. The entire system is controlled remotely via a well-designed, user-friendly web interface, featuring dynamic network configuration and Over-The-Air (OTA) firmware update capabilities. 

## **Core Hardware Components** 

- **Actuators:** PWM-controlled Ventilation Fan, Servo Motor (for curtain operation). 

- **Sensors:** Temperature Sensor. 

- **Indicators:** RGB LED (for ambient lighting), Red LED (for temperature alarms). 

- **Microcontroller:** Wi-Fi capable MCU (e.g., ESP32) to handle the web server, networking, and hardware control. 

## **System Features & Operational Modes** 

## **1. Curtain Actuation (Servo Motor)** 

The system controls physical curtains or blinds via a servo motor, offering two modes of operation: 

- **Automatic Scheduled Mode:** A time-based scheduling system that stores at least **programmable records** . Each record consists of a specific time (Hour) and the target curtain opening percentage. 

- **Manual Mode:** Direct override where the user can input the exact desired opening percentage for the curtains. 

## **2. Temperature & Ventilation Control (Fan & Red LED)** 

The fan speed is dynamically controlled using PWM based on temperature readings, with two modes: 

- **Automatic Temperature Mode:** A closed-loop proportional control system. The user inputs a _Desired Temperature_ and a _Maximum Temperature_ . 

   - The fan operates at **0%** (off) when the ambient temperature is at or below the Desired Temperature. 

   - The fan operates at **100%** (maximum speed) when the temperature hits the Maximum Temperature. 

   - For temperatures falling between the desired and maximum thresholds, the fan speed scales proportionally. 

   - **Alarm Trigger:** If the ambient temperature exceeds the defined Maximum Temperature, a Red LED must blink continuously with a 1Hz frequency (1 second ON, 1 second OFF) as a warning. 

- **Manual Temperature Mode:** The user bypasses the sensor and manually sets the fan speed via a percentage input ( **0%** to **100%** ). 

## **3. Ambient Lighting (RGB LED)** 

- **Light Mode:** The user has full control over an RGB LED, with the ability to program both the specific color and the brightness intensity. 

## **Connectivity & Web Interface** 

- **Centralized Web Control:** Every operational mode, threshold, and schedule mentioned above must be controllable exclusively through a well-presented and intuitive web interface. 

- **Dynamic Network Configuration:** The web dashboard must include input fields allowing the user to enter a new Wi-Fi SSID and Password, enabling the device to connect to different local networks without needing to be hardcoded. 

- **Access Point Management:** The device must allow the user to modify its own broadcasted SSID and Password (Soft-AP credentials) directly from the webpage. 

- **OTA Support:** The system architecture must support Over-The-Air (OTA) programming, allowing for wireless firmware updates via the network. 

