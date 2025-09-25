# ESP32-S3-SIM7670G GPS Tracker - Copilot Instructions

## Project Overview

This is a professional-grade ESP32-S3-SIM7670G GPS tracker featuring a fully modular architecture. The device collects GPS location and battery data, transmitting it via MQTT over 4G cellular every 30 seconds.

## Architecture

**Modular Design**: The project follows a clean interface-based modular architecture:

- `main/config.h/c` - Centralized configuration system with NVS storage
- `main/modules/gps/` - GPS module (SIM7670G GNSS functionality)  
- `main/modules/lte/` - LTE/Cellular module (SIM7670G network connectivity)
- `main/modules/mqtt/` - MQTT communication module
- `main/modules/battery/` - Battery monitoring module (MAX17048 fuel gauge)
- `main/gps_tracker.c` - Main application logic

Each module provides:
- Clean function pointer-based interfaces
- Independent configuration structures
- Standardized initialization patterns
- Built-in debugging and status reporting
- Easy testing and maintenance

## Technical Stack

- **Framework**: ESP-IDF v5.1+
- **Target**: ESP32-S3 (dual-core, 240MHz)
- **Hardware**: ESP32-S3-SIM7670G development board
- **Connectivity**: 4G/LTE via SIM7670G module
- **GPS**: Integrated GNSS in SIM7670G
- **Battery**: MAX17048 fuel gauge via I2C
- **Communication**: UART (SIM7670G), I2C (battery), MQTT over cellular

## Configuration

**Pre-configured settings**:
- MQTT Broker: `65.124.194.3:1883`
- APN: `m2mglobal`
- Topic: `gps_tracker/data`
- Transmission Interval: 30 seconds
- Battery Thresholds: Low (15%), Critical (5%)

**Configuration System**: Centralized config with NVS persistence, runtime modification support.

## Key Features

- **Modular Architecture**: Easy to maintain, test, and extend
- **Real-time GPS Tracking**: NMEA parsing, fix validation
- **Cellular Connectivity**: AT command interface, network management
- **MQTT Communication**: JSON payload transmission
- **Battery Monitoring**: Voltage, percentage, charging status
- **Debug Support**: Per-module debug flags and comprehensive logging
- **Persistent Storage**: Configuration saved to NVS flash

## Development Guidelines

When working on this project:

1. **Respect Module Boundaries**: Use defined interfaces, don't access internals directly
2. **Follow Interface Pattern**: All modules use function pointer interfaces
3. **Use Centralized Config**: All settings go through `config.h/c` system
4. **Maintain Debug Support**: Each module should support debug output flags
5. **Test Independently**: Modules should be testable in isolation
6. **Update Documentation**: Keep README.md current with changes

## Common Tasks

- **Adding Features**: Create new modules following existing interface patterns
- **Configuration Changes**: Modify structures in `config.h`, update defaults in `config.c`
- **Hardware Changes**: Update pin definitions in respective module configs
- **Protocol Changes**: Modify module implementations while keeping interfaces stable
- **Testing**: Use module interfaces for independent testing

## Build and Flash

### ESP-IDF Environment Setup

**ESP-IDF Installation Location**: `C:\Espressif\frameworks\esp-idf-v5.5`

**Required Command to Setup Environment and Build**:
```powershell
cd "C:\Espressif\frameworks\esp-idf-v5.5"; .\export.ps1; cd "c:\Users\dom\Documents\esp-idf-tracker"; idf.py build
```

### Working Commands Log

**Commands that have been tested and work**:
- ✅ `cd "C:\Espressif\frameworks\esp-idf-v5.5"; .\export.ps1; cd "c:\Users\dom\Documents\esp-idf-tracker"; idf.py --version` - ESP-IDF v1.0.3 confirmed
- ✅ `idf.py set-target esp32s3` - Target set successfully
- ✅ `cd "C:\Espressif\frameworks\esp-idf-v5.5"; .\export.ps1; cd "c:\Users\dom\Documents\esp-idf-tracker"; idf.py build` - **BUILD SUCCESSFUL!** ✅ Complete compilation with no errors
- ❌ `idf.py build` - Fails without environment setup, needs full command above

**Project States & Backup Protocol**:
- 🔄 **Always backup before compile/build operations**
- 📁 **Keep 5 versions**: `esp-idf-tracker-backup-v1` through `esp-idf-tracker-backup-v5`
- 💾 **Backup locations**: `c:\Users\dom\Documents\` with version suffix
- ⚡ **Auto-rotate**: Delete oldest backup when creating new one
- 🛡️ **Safety rule**: Never edit without recent backup

**Current Project State**: ✅ **GITHUB READY** - Complete modular architecture with versioning system! 
- ✅ Modular architecture compiled successfully
- ✅ Secure config template system (APN/MQTT as placeholders) 
- ✅ Git repository initialized with proper .gitignore
- ✅ GitHub Actions workflow for CI/CD
- ✅ Complete documentation and contribution guidelines
- ✅ Comprehensive versioning system (v1.0.0)
- ✅ Ready to push to GitHub as public repository "ESP32-S3-SIM7670G-GPS-Tracker"

**⚠️ CRITICAL DEVELOPMENT RULE: ALWAYS ADVANCE VERSIONING**
- Every code change MUST bump version (patch/minor/major)
- Use `python update_version.py --bump [type]` before any changes
- Use `.\bump_and_commit.ps1 [type] "message"` for quick version+commit
- See VERSIONING_WORKFLOW.md for complete process

**📚 ALWAYS REFERENCE - Waveshare ESP32-S3-SIM7670G-4G Official Documentation:**
- **Overview**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#Overview
- **Cat-1 Module AT Commands**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#Cat-1_Module_Command_Set
- **HTTP Implementation**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#HTTP
- **MQTT Implementation**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#MQTT
- **GNSS/GPS Module**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#GNSS
- **Demo Examples**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#Demo_Explaination
- **Camera Interface**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#Camera
- **TF-Card Support**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#TF-Card
- **RGB LED Control**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#RGB
- **Battery Management**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#BAT
- **WiFi Functionality**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#Portable_WIFI_Demo
- **Cloud Applications**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#Waveshare_Cloud_Application
- **Resources & Downloads**: https://www.waveshare.com/wiki/ESP32-S3-SIM7670G-4G#Resource

**⚡ ESP-IDF WORKFLOW RULES (CRITICAL):**
- **COM Port Management**: ALWAYS **KILL** serial monitor processes before building/flashing - don't just close!
- **Kill Processes**: `taskkill /f /im python.exe` to force kill ESP monitor processes
- **Environment Setup**: ALWAYS use: `cd "C:\Espressif\frameworks\esp-idf-v5.5"; .\export.ps1; cd "c:\Users\dom\Documents\esp-idf-tracker"`
- **Development Sequence**: 1) **KILL** monitor processes 2) Build 3) Flash 4) Monitor
- **Clean Builds**: Use `idf.py fullclean` when config changes don't apply

### Using VS Code ESP-IDF Extension

1. Set target: ESP32-S3
2. Build project
3. Flash via UART
4. Monitor output

### Manual Build Steps

1. **Setup Environment**: `cd "C:\Espressif\frameworks\esp-idf-v5.5"; .\export.ps1`
2. **Navigate to Project**: `cd "c:\Users\dom\Documents\esp-idf-tracker"`
3. **Set Target**: `idf.py set-target esp32s3`
4. **Build**: `idf.py build`
5. **Flash**: `idf.py -p COMx flash` (replace COMx with actual port)
6. **Monitor**: `idf.py -p COMx monitor`

The project is ready to compile and flash with the ESP-IDF development environment.