# ESP32-S3-SIM7670G GPS Tracker

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](VERSION)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5+-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Hardware](https://img.shields.io/badge/hardware-ESP32--S3--SIM7670G-orange.svg)](https://www.waveshare.com/esp32-s3-sim7670g.htm)
[![License](https://img.shields.io/badge/license-MIT-purple.svg)](LICENSE)
[![Development Status](https://img.shields.io/badge/status-working-brightgreen.svg)](#)

> **✅ WORKING STATUS**: This project is fully functional! GPS tracking with cellular MQTT transmission is working. The modular architecture is complete and tested. Ready for production use and contributions welcome!

A modular GPS tracking device built for the ESP32-S3-SIM7670G development board, featuring cellular connectivity and battery monitoring with a clean, maintainable architecture.

## 🎯 Key Features - FULLY WORKING

- ✅ **GPS Location Tracking**: Real-time coordinate acquisition via SIM7670G GNSS
- ✅ **4G/LTE Cellular**: Full network connectivity with proper AT command handling  
- ✅ **MQTT Data Transmission**: JSON payload publishing every 30 seconds
- ✅ **Battery Monitoring**: MAX17048 fuel gauge with voltage and percentage
- ✅ **Modular Architecture**: Clean interfaces for easy maintenance and testing
- ✅ **GPS Port Switching**: Proper NMEA data handling following Waveshare documentation
- ✅ **Network Auto-Recovery**: Robust connection management with retry logic
- ✅ **Debug Logging**: Comprehensive status reporting for troubleshooting

## Overview

This project implements a GPS tracker that:
- 📍 Collects GPS location data using the integrated SIM7670G GNSS module
- 🔋 Monitors battery status using the MAX17048 fuel gauge IC
- 📡 Transmits data via MQTT over 4G/LTE cellular connection
- ⏰ Sends location and battery data every 30 seconds (configurable)
- 🧩 Features fully modular architecture for easy maintenance and testing
- ⚙️ Includes centralized configuration system with NVS storage
- 🐛 Built-in debugging and status reporting for each module

## Modular Architecture

The project follows a clean modular design pattern with separate modules for each major component:

```
main/
├── config.h/c              # Centralized configuration system
├── gps_tracker.c            # Main application logic
├── modules/
│   ├── gps/                 # GPS module (SIM7670G GNSS)
│   │   ├── gps_module.h     # Interface definition
│   │   └── gps_module.c     # NMEA parsing, GPS data acquisition
│   ├── lte/                 # LTE/Cellular module (SIM7670G)
│   │   ├── lte_module.h     # Interface definition  
│   │   └── lte_module.c     # AT commands, network management
│   ├── mqtt/                # MQTT communication module
│   │   ├── mqtt_module.h    # Interface definition
│   │   └── mqtt_module.c    # MQTT client, JSON payload handling
│   └── battery/             # Battery monitoring module (MAX17048)
│       ├── battery_module.h # Interface definition
│       └── battery_module.c # I2C communication, fuel gauge
└── CMakeLists.txt
```

### Module Benefits

Each module provides:
- ✨ **Clean Interface**: Function pointer-based interfaces for easy testing
- 🔧 **Independent Configuration**: Separate config structures for each module  
- 🚀 **Standardized Patterns**: Consistent initialization and error handling
- 📊 **Built-in Debugging**: Status reporting and diagnostic information
- 🧪 **Easy Testing**: Modules can be tested independently
- 🔄 **Hot-swappable**: Interfaces allow easy component replacement

## Hardware Requirements

- **ESP32-S3-SIM7670G Development Board** (Waveshare or similar)
  - ESP32-S3 dual-core microcontroller (240MHz)
  - SIM7670G 4G/LTE/GNSS module
  - MAX17048 battery fuel gauge IC
  - 18650 battery holder
- **4G SIM Card** with active data plan
- **18650 Li-ion Battery** (3.7V, 2000mAh+ recommended)
- **GPS Antenna** (connected to GNSS IPEX connector)
- **4G/LTE Antenna** (connected to 4G IPEX connector)

## Pin Configuration

| Function | ESP32-S3 Pin | SIM7670G/IC | Description |
|----------|--------------|-------------|-------------|
| UART1 TX | GPIO17 | SIM7670G RX | AT command communication |
| UART1 RX | GPIO18 | SIM7670G TX | AT response reception |
| I2C SDA  | GPIO3  | MAX17048 SDA | Battery monitor data |
| I2C SCL  | GPIO2  | MAX17048 SCL | Battery monitor clock |
| Power Enable | GPIO4 | SIM7670G PWRKEY | Module power control |

## Configuration System

The project uses a flexible configuration system that separates sensitive settings from the codebase:

### Quick Setup

1. **Copy Configuration Template**:
   ```bash
   cp config.template.h main/config_user.h
   ```

2. **Edit Your Settings**:
   Open `main/config_user.h` and configure:
   - Your cellular APN (contact your provider)
   - Your MQTT broker details
   - Device-specific settings

### System Configuration Structure

```c
typedef struct {
    gps_config_t gps;           // GPS module settings
    lte_config_t lte;           // Cellular network settings  
    mqtt_config_t mqtt;         // MQTT broker settings
    battery_config_t battery;   // Battery monitoring settings
    system_config_t system;     // System-wide settings
} tracker_system_config_t;
```

### Configuration Options

- **APN Configuration**: Set your cellular provider's APN
- **MQTT Broker**: Configure your MQTT server details  
- **Device ID**: Set unique identifier for your tracker
- **Transmission Interval**: Customize data upload frequency
- **Battery Thresholds**: Set low/critical battery warnings

### Runtime Configuration

Settings can be modified at runtime and persist in NVS flash:

```c
// Load current configuration
tracker_system_config_t config;
config_load_from_nvs(&config);

// Modify settings
config.mqtt_config.publish_interval_ms = 60000; // 1 minute
strcpy(config.mqtt_config.broker, "new.broker.ip");

// Save to flash
config_save_to_nvs(&config);
```

## Hardware Setup

### Physical Connections

1. **SIM Card Installation**
   - Insert activated 4G SIM card into the designated slot
   - Ensure proper orientation (usually marked on PCB)

2. **Antenna Connections**
   - Connect GPS antenna to GNSS IPEX connector (⑳)
   - Connect 4G antenna to 4G IPEX connector (📡)
   - Ensure antennas have clear view of sky for GPS

3. **Power Setup**
   - Install 18650 battery (observe polarity markings: + and -)
   - Or use USB-C for development/testing
   - Board includes charging circuit for battery

4. **DIP Switch Configuration**
   - Set 4G switch to ON position
   - Configure other switches as per board documentation

## Getting Started

### 1. Clone Repository

```bash
git clone https://github.com/yourusername/ESP32-S3-SIM7670G-4G.git
cd ESP32-S3-SIM7670G-4G
```

### 2. Configure Your Settings

```bash
# Copy the configuration template
cp config.template.h main/config_user.h

# Edit with your specific settings
# - Cellular APN (contact your provider)
# - MQTT broker details
# - Device identifiers
```

**⚠️ Important**: Never commit `main/config_user.h` to version control as it contains sensitive information.

## Software Setup

### Prerequisites

1. **ESP-IDF Framework v5.1+**
   ```bash
   # Windows (using installer)
   https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html
   
   # Linux/macOS
   mkdir -p ~/esp
   cd ~/esp
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32s3
   . ./export.sh
   ```

2. **VS Code ESP-IDF Extension**
   - Open VS Code
   - Install "Espressif IDF" extension
   - Configure ESP-IDF path in extension settings

### Building and Flashing

#### Method 1: VS Code (Recommended)

1. Open project folder in VS Code
2. Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on macOS)
3. Run these commands in sequence:
   ```
   ESP-IDF: Set Espressif Device Target → ESP32-S3
   ESP-IDF: Build your Project
   ESP-IDF: Flash (UART) your Project  
   ESP-IDF: Monitor your Device
   ```

#### Method 2: Command Line

```bash
# Set target
idf.py set-target esp32s3

# Configure (optional - defaults should work)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p COM3 flash monitor  # Windows
idf.py -p /dev/ttyUSB0 flash monitor  # Linux
```

### Expected Output

After successful flash and boot:

```
I (1234) GPS_TRACKER: ESP32-S3-SIM7670G GPS Tracker starting...
I (1235) CONFIG: Configuration system initialized
I (1240) GPS_TRACKER: Initializing modules...
I (1250) BATTERY_MODULE: Battery module initialized successfully
I (1260) GPS_MODULE: GPS module initialized successfully  
I (1270) LTE_MODULE: LTE module initialized successfully
I (1280) LTE_MODULE: Connecting to network...
I (5000) LTE_MODULE: Network connected, IP: 10.xxx.xxx.xxx
I (5010) MQTT_MODULE: MQTT module initialized successfully
I (5020) GPS_TRACKER: GPS Tracker initialization complete
I (5030) GPS_TRACKER: Transmission timer started (interval: 30000 ms)
```

## Data Format

The tracker publishes JSON data to the configured MQTT topic:

```json
{
  "timestamp": "2024-01-15T10:30:45Z",
  "gps": {
    "latitude": 37.7749,
    "longitude": -122.4194,
    "altitude": 100.5,
    "speed_kmh": 25.3,
    "course": 180.5,
    "satellites": 8,
    "fix_valid": true
  },
  "battery": {
    "percentage": 85.5,
    "voltage": 3.85,
    "charging": false,
    "present": true
  }
}
```

## Module APIs

### GPS Module (`modules/gps/`)

```c
// Initialize GPS module
gps_interface_t* gps_if = gps_get_interface();
gps_if->init(&gps_config);

// Read GPS data
gps_data_t gps_data;
gps_if->read_data(&gps_data);
```

### LTE Module (`modules/lte/`)

```c
// Initialize and connect
lte_interface_t* lte_if = lte_get_interface();
lte_if->init(&lte_config);

// Check connection status
bool connected = lte_if->is_connected();
```

### MQTT Module (`modules/mqtt/`)

```c
// Initialize and publish
mqtt_interface_t* mqtt_if = mqtt_get_interface(); 
mqtt_if->init(&mqtt_config);
mqtt_if->publish("topic", "payload", strlen(payload));
```

### Battery Module (`modules/battery/`)

```c
// Initialize and read
battery_interface_t* batt_if = battery_get_interface();
batt_if->init(&battery_config);

battery_data_t battery_data;
batt_if->read_data(&battery_data);
```

## Troubleshooting

### Common Issues

1. **No GPS Fix**
   - Ensure GPS antenna is properly connected
   - Move to location with clear sky view
   - Wait 2-5 minutes for initial fix (cold start)
   - Check GNSS LED indicator on board

2. **Cellular Connection Failed**
   - Verify SIM card is activated and has data plan
   - Check signal strength indicators
   - Confirm APN settings match carrier requirements
   - Ensure 4G antenna is connected

3. **MQTT Connection Issues**
   - Verify broker IP address and port
   - Check cellular data connectivity
   - Confirm broker allows anonymous connections
   - Monitor logs for authentication errors

4. **Battery Reading Issues**
   - Check I2C connections (SDA/SCL pins)
   - Verify MAX17048 IC is present on board
   - Ensure battery is properly installed
   - Check for loose connections

### Debug Mode

Enable detailed debugging by modifying `config.c`:

```c
// Set debug flags for specific modules
config.gps_config.debug_output = true;
config.lte_config.debug_output = true;
config.mqtt_config.debug_output = true;
config.battery_config.debug_output = true;
```

### Log Levels

Adjust ESP-IDF log levels via menuconfig:
```
Component config → Log output → Default log verbosity → Debug
```

## Development

### Adding New Features

The modular architecture makes it easy to add new functionality:

1. **Create new module** in `main/modules/new_module/`
2. **Define interface** in `new_module.h`
3. **Implement interface** in `new_module.c`
4. **Add to CMakeLists.txt** in SRCS and INCLUDE_DIRS
5. **Include in main application** and initialize

### Testing Modules

Each module can be tested independently:

```c
// Test GPS module standalone
gps_interface_t* gps_if = gps_get_interface();
assert(gps_if != NULL);
assert(gps_if->init(&test_config) == true);
```

## License

This project is provided as-is for educational and development purposes.

## Support

For issues and questions:
1. Check the troubleshooting section above
2. Review ESP-IDF documentation: https://docs.espressif.com/
3. Check SIM7670G AT command manual for cellular issues
4. Verify hardware connections and component specifications

## Credits

- Built on Espressif ESP-IDF framework
- SIM7670G module documentation and AT commands
- MAX17048 fuel gauge specifications
- Modular architecture inspired by embedded systems best practices