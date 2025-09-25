#ifndef VERSION_H
#define VERSION_H

// Project Version Information
#define PROJECT_VERSION_MAJOR 1
#define PROJECT_VERSION_MINOR 0  
#define PROJECT_VERSION_PATCH 0
#define PROJECT_VERSION_STRING "1.0.0"

// Build Information
#define PROJECT_NAME "ESP32-S3-SIM7670G-GPS-Tracker"
#define PROJECT_DESCRIPTION "Modular 4G GPS Tracker with MQTT"
#define PROJECT_AUTHOR "ESP-IDF GPS Tracker Project"

// Hardware Version Compatibility
#define HARDWARE_VERSION "ESP32-S3-SIM7670G"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// Feature Version Flags
#define SUPPORTS_GPS 1
#define SUPPORTS_4G_LTE 1
#define SUPPORTS_MQTT 1 
#define SUPPORTS_BATTERY_MONITOR 1

// Version Helper Macros
#define MAKE_VERSION(major, minor, patch) ((major << 16) | (minor << 8) | patch)
#define PROJECT_VERSION_NUMBER MAKE_VERSION(PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH)

// Version String Helpers
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Full Version Info Function Declaration
const char* get_version_info(void);
const char* get_build_info(void);

#endif // VERSION_H