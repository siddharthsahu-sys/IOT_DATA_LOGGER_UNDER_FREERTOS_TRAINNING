# IoT Data Logger with MQTT Configuration

## Overview

This IoT Data Logger is an ESP32-based solution that collects sensor data via UART, publishes it to an MQTT broker, and supports remote configuration updates. The system is designed for industrial monitoring applications where sensors communicate over serial interfaces and data needs to be reliably transmitted to cloud platforms.

## Key Features

### 1. **Dual-Mode Data Collection**
- **Query-Based Mode**: Sends predefined queries to sensors and collects responses
- **Continuous Mode**: Continuously reads data from sensors without queries

### 2. **Configurable Parameters**
- Publish frequency
- Number of queries
- Query strings (up to 10 queries, 20 bytes each)
- Query-based or continuous operation mode

### 3. **Remote Configuration via MQTT**
- Server can push new configurations to the device
- Configuration is stored in NVS (Non-Volatile Storage)
- Device auto-restarts after applying new configuration
- Acknowledgment messages confirm successful configuration updates

### 4. **Robust Data Handling**
- Queue-based architecture to handle WiFi connectivity issues
- 50-element queue buffers data during network interruptions
- Simulated WiFi burst testing to validate queue performance

### 5. **Dual-Core Processing**
- **Core 1**: UART sampling and data collection
- **Core 0**: MQTT publishing and network operations

### 6. **Health Monitoring**
- Real-time heap memory monitoring
- Task stack usage tracking
- Queue status reporting

## Hardware Requirements

- ESP32 Development Board
- UART-compatible sensor(s)
- WiFi network access
- MQTT broker (e.g., Eclipse Mosquitto, HiveMQ Cloud)

## Pin Configuration

| Pin | Function |
|-----|----------|
| GPIO 17 | UART TX |
| GPIO 16 | UART RX |

## Software Dependencies

- ESP-IDF v4.4 or later
- MQTT client library
- Protocol examples common library

## Configuration Macros

The following macros should be defined in `macros.h`:

```c
#define DEFAULT_MQTTURL      "mqtt://your-broker-url"
#define DEFAULT_USERNAME     "your-username"
#define DEFAULT_PASSWORD     "your-password"