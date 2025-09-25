/**
 * Configuration Template for ESP32-S3-SIM7670G GPS Tracker
 * 
 * Copy this file to main/config_user.h and modify the values below
 * to match your specific setup. This allows you to customize settings
 * without modifying the main codebase.
 * 
 * If config_user.h exists, it will override the default values.
 */

#pragma once

// =============================================================================
// NETWORK CONFIGURATION
// =============================================================================

// Your APN settings (contact your cellular provider)
#define USER_CONFIG_APN              "your-apn-here"          // e.g., "hologram", "m2mglobal", "iot.1nce.net"
#define USER_CONFIG_APN_USERNAME     ""                       // Usually empty for most providers
#define USER_CONFIG_APN_PASSWORD     ""                       // Usually empty for most providers

// =============================================================================
// MQTT BROKER CONFIGURATION  
// =============================================================================

#define USER_CONFIG_MQTT_BROKER      "your-mqtt-broker.com"  // Your MQTT broker IP or hostname
#define USER_CONFIG_MQTT_PORT        1883                     // MQTT port (1883 for non-SSL, 8883 for SSL)
#define USER_CONFIG_MQTT_USERNAME    ""                       // MQTT username (if required)
#define USER_CONFIG_MQTT_PASSWORD    ""                       // MQTT password (if required)
#define USER_CONFIG_MQTT_CLIENT_ID   "esp32_gps_tracker"     // Unique client ID for your device
#define USER_CONFIG_MQTT_TOPIC       "tracker/location"      // MQTT topic to publish location data

// =============================================================================
// DEVICE CONFIGURATION
// =============================================================================

#define USER_CONFIG_DEVICE_ID        "GPS_TRACKER_001"       // Unique device identifier
#define USER_CONFIG_UPDATE_INTERVAL  30000                   // Data transmission interval in milliseconds

// =============================================================================
// ADVANCED SETTINGS (Optional)
// =============================================================================

// GPS Settings
#define USER_CONFIG_GPS_MIN_SATS     4                       // Minimum satellites for valid fix
#define USER_CONFIG_GPS_TIMEOUT      60000                   // GPS fix timeout in milliseconds

// Battery Monitoring
#define USER_CONFIG_LOW_BATTERY      15.0f                   // Low battery warning threshold (%)
#define USER_CONFIG_CRITICAL_BATTERY 5.0f                    // Critical battery threshold (%)

// Debug Settings  
#define USER_CONFIG_DEBUG_GPS        true                    // Enable GPS debug output
#define USER_CONFIG_DEBUG_LTE        true                    // Enable LTE debug output
#define USER_CONFIG_DEBUG_MQTT       true                    // Enable MQTT debug output
#define USER_CONFIG_DEBUG_BATTERY    true                    // Enable battery debug output