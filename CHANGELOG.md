# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.1] - 2025-09-25 - 🎯 GPS PORT SWITCHING FIX

### Fixed
- **GPS Initialization Error**: Removed undocumented `AT+CGNSSPORTSWITCH` command that was causing GPS initialization failures
- **GPS Power On**: Now using Waveshare official method with only `AT+CGNSSPWR=1` and `AT+CGNSSTST=1`
- **Port Switching Issues**: Eliminated all port switching commands per Waveshare documentation
- **System Stability**: No more GPS-related errors during initialization sequence

### Changed  
- `modem_init.c`: Removed problematic `AT+CGNSSPORTSWITCH=0,1` command from GPS initialization
- `gps_module.c`: Removed port switching commands from power on/off functions
- `lte_module.c`: Cleaned up GPS interference prevention code
- Updated all comments to reflect Waveshare official approach

### Technical Details
- **Root Cause**: `AT+CGNSSPORTSWITCH` command not documented in official Waveshare ESP32-S3-SIM7670G reference
- **Solution**: Follow Waveshare documentation exactly - use only documented GNSS commands
- **Result**: GPS initialization now works perfectly with `AT+CGNSSPWR=1` → `OK` (343ms response)
- **Status**: ✅ GPS ready for outdoor satellite acquisition testing

## [1.0.0] - 2025-09-25 - 🚧 DEVELOPMENT MILESTONE

### Added
- **Initial Release**: Complete ESP32-S3-SIM7670G GPS Tracker implementation
- **Modular Architecture**: Clean separation of GPS, LTE, MQTT, and Battery modules
- **GPS Module**: SIM7670G GNSS integration with NMEA parsing and coordinate conversion
- **LTE Module**: 4G cellular connectivity with AT command interface and network management
- **MQTT Module**: JSON data transmission over cellular connection
- **Battery Module**: MAX17048 fuel gauge integration with I2C communication
- **Configuration System**: Centralized config management with NVS persistence
- **Security Features**: Template-based configuration for secure public repository sharing
- **Version System**: Comprehensive versioning with build information display
- **Build System**: ESP-IDF v5.5 compatible build configuration
- **Documentation**: Complete README, contribution guidelines, and setup instructions
- **CI/CD**: GitHub Actions workflow for automated building and testing

### Working Components
- ✅ **4G/LTE Connectivity**: Complete cellular network registration and data connection
- ✅ **Modular Architecture**: Clean interface-based design with proper separation
- ✅ **UART Communication**: Optimized character-by-character transmission with delays
- ✅ **Network Registration**: Successful cellular connectivity with APN configuration
- ✅ **AT Command System**: Robust response handling with comprehensive debugging
- ✅ **Signal Quality**: Good cellular signal strength (CSQ: 21/31)

### Issues Identified
- ❌ **MQTT Service**: AT+CMQTTSTART fails with timeout (needs investigation)
- ❌ **GPS Port Switching**: AT+CGNSSPORTSWITCH=0,1 returns ERROR
- 🟡 **GPS Location**: Module initializes but position fix requires outdoor testing
- 🟡 **Battery Functions**: MAX17048 detected but full functionality needs verification

### Technical Specifications
- **Target Hardware**: ESP32-S3-SIM7670G development board
- **Framework**: ESP-IDF v5.5+
- **Communication**: UART (SIM7670G), I2C (MAX17048), MQTT over 4G/LTE
- **Data Transmission**: GPS location + battery status every 30 seconds
- **Configuration**: APN "m2mglobal", MQTT broker configurable
- **Power Management**: Battery monitoring with low/critical thresholds

### Features
- Real-time GPS tracking with fix validation
- Automatic cellular network connection and management
- JSON payload transmission via MQTT
- Battery voltage, percentage, and charging status monitoring
- Modular design for easy maintenance and testing
- Debug support with per-module logging control
- Persistent configuration storage in NVS flash
- Professional error handling and recovery mechanisms

### Build System
- CMake-based build configuration
- ESP-IDF component registration
- Version integration from VERSION file
- Automated dependency management

### Documentation
- Comprehensive README with setup instructions
- Detailed module documentation and API references
- Contributing guidelines for developers
- GitHub setup instructions with security considerations
- Hardware compatibility and pin configuration details

[Unreleased]: https://github.com/yourusername/ESP32-S3-SIM7670G-4G/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/yourusername/ESP32-S3-SIM7670G-4G/releases/tag/v1.0.0