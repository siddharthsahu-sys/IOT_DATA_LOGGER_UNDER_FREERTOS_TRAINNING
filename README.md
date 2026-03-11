IoT Data Logger with MQTT Configuration
Overview
This IoT Data Logger is an ESP32-based solution that collects sensor data via UART, publishes it to an MQTT broker, and supports remote configuration updates. The system is designed for industrial monitoring applications where sensors communicate over serial interfaces and data needs to be reliably transmitted to cloud platforms.

Key Features
1. Dual-Mode Data Collection
Query-Based Mode: Sends predefined queries to sensors and collects responses

Continuous Mode: Continuously reads data from sensors without queries

2. Configurable Parameters
Publish frequency

Number of queries

Query strings (up to 10 queries, 20 bytes each)

Query-based or continuous operation mode

3. Remote Configuration via MQTT
Server can push new configurations to the device

Configuration is stored in NVS (Non-Volatile Storage)

Device auto-restarts after applying new configuration

Acknowledgment messages confirm successful configuration updates

4. Robust Data Handling
Queue-based architecture to handle WiFi connectivity issues

50-element queue buffers data during network interruptions

Simulated WiFi burst testing to validate queue performance

5. Dual-Core Processing
Core 1: UART sampling and data collection

Core 0: MQTT publishing and network operations

6. Health Monitoring
Real-time heap memory monitoring

Task stack usage tracking

Queue status reporting

Hardware Requirements
ESP32 Development Board

UART-compatible sensor(s)

WiFi network access

MQTT broker (e.g., Eclipse Mosquitto, HiveMQ Cloud)

Pin Configuration
Pin	Function
GPIO 17	UART TX
GPIO 16	UART RX
Software Dependencies
ESP-IDF v4.4 or later

MQTT client library

Protocol examples common library

Configuration Macros
The following macros should be defined in macros.h:

c
#define DEFAULT_MQTTURL      "mqtt://your-broker-url"
#define DEFAULT_USERNAME     "your-username"
#define DEFAULT_PASSWORD     "your-password"
MQTT Topics
Data Publishing
Topic Format: ESP32toCloud/[MAC_ADDRESS]

Payload Format:

Query mode: ~[response1]#~[response2]#...

Continuous mode: ~[data]#

Configuration
Subscribe Topic: Config/[MAC_ADDRESS]

Publish Topic: Config/[MAC_ADDRESS] (for acknowledgments)

Configuration Push from Server
Overview
The device supports over-the-air configuration updates via MQTT. This allows remote management of multiple devices without physical access.

Configuration Flow
text
Server                      Device
  |                           |
  |-- Config Payload (Hex) -->|
  |                           |-- Parse & Validate
  |                           |-- Store in NVS
  |                           |-- Auto-Restart
  |<-- "SUCCESS" Acknowledge--|
  |                           |
  |-- New Config Active ----->|
Configuration Payload Format
The configuration is sent as a hexadecimal string representing the binary device_config_t structure:

c
typedef struct __attribute__((packed)) {
    uint32_t publish_freq;      // Publishing interval in seconds
    uint8_t is_query_based;      // 1 = query mode, 0 = continuous
    uint8_t num_queries;         // Number of queries (0-10)
    uint8_t query_lengths[10];   // Length of each query
    uint8_t queries[10][20];      // Query strings
} device_config_t;
Example Configuration Push
Using mosquitto_pub:

bash
# Convert config to hex and publish
mosquitto_pub -t "Config/001122334455" -m "010000000a0104010203ff04010203ff"
Configuration Acknowledgment
The device responds with:

SUCCESS - Configuration applied successfully

FAIL_LEN - Invalid payload length

Security Considerations
Certificate Validation: TLS certificate verification is enabled by default

Authentication: MQTT username/password authentication supported

MAC-based Topics: Each device uses its unique MAC address for topics

Queue-Based Architecture
The system uses a FreeRTOS queue to decouple data collection from network transmission:

text
UART Sampler → [QUEUE] → MQTT Publisher
    (Core 1)    (50 items)    (Core 0)
This architecture ensures:

Data is never lost during network interruptions

Sampler continues collecting even when WiFi is busy

Burst data can be handled efficiently

Building and Flashing
bash
# Set target
idf.py set-target esp32

# Build the project
idf.py build

# Flash to device
idf.py -p /dev/ttyUSB0 flash

# Monitor output
idf.py -p /dev/ttyUSB0 monitor
Configuration Options
Menuconfig Settings
Navigate to idf.py menuconfig and configure:

WiFi Configuration

SSID and password

Connection timeout

MQTT Configuration

Broker URL

Username/password

Keep-alive interval

UART Configuration

Baud rate (default: 9600)

Data bits

Stop bits

Parity

Default Configuration
If no stored configuration exists, the device uses:

c
{
    .publish_freq = 10,           // 10 seconds
    .is_query_based = 1,           // Query mode
    .num_queries = 2,              // Two queries
    .query_lengths = {4, 4},       // 4 bytes each
    .queries = {
        {0x01, 0x02, 0x03, 0xFF},  // Query 1
        {0x02, 0x02, 0x03, 0xFF}   // Query 2
    }
}
Health Monitoring Output
The system prints periodic health statistics:

text
--- HEALTH CHECK ---
Free Heap: 123456 bytes
Sampler Stack Left: 1024
MQTT Task Stack Left: 896
Queue Status: 5/50 items
--------------------
Troubleshooting
Common Issues
WiFi Connection Failures

Check SSID and password

Verify network availability

Check antenna connection

MQTT Connection Failures

Verify broker URL

Check credentials

Ensure network connectivity

UART Communication Issues

Verify baud rate matching

Check wiring connections

Validate logic levels

Queue Full Warnings

Increase QUEUE_SIZE if persistent

Check network stability

Reduce publish frequency

Performance Metrics
Maximum Queue Size: 50 messages

Message Size: Up to 500 bytes

Publish Frequency: Configurable (default 10 seconds)

WiFi Burst Simulation: Every 15 messages (4-second delay)

Future Enhancements
OTA Updates: Add firmware update capability

Multiple UART Ports: Support for multiple sensors

Data Encryption: End-to-end encryption for sensitive data

Time-Series Database: Local data storage with timestamps

Advanced Scheduling: Configurable sampling schedules

License
This project is based on ESP-IDF examples and is available under the Apache License 2.0.

Support
For issues and questions:

Check ESP-IDF documentation

Review MQTT broker documentation

Verify hardware connections

Monitor serial output for error messages

