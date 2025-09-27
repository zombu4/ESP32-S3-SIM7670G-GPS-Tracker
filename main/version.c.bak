#include "version.h"
#include <stdio.h>
#include <string.h>

static char version_info_buffer[256];
static char build_info_buffer[256];

const char* get_version_info(void) {
    snprintf(version_info_buffer, sizeof(version_info_buffer),
        "%s v%s\n"
        "Hardware: %s\n"
        "Features: GPS=%s, LTE=%s, MQTT=%s, Battery=%s",
        PROJECT_NAME,
        PROJECT_VERSION_STRING,
        HARDWARE_VERSION,
        SUPPORTS_GPS ? "Yes" : "No",
        SUPPORTS_4G_LTE ? "Yes" : "No", 
        SUPPORTS_MQTT ? "Yes" : "No",
        SUPPORTS_BATTERY_MONITOR ? "Yes" : "No"
    );
    return version_info_buffer;
}

const char* get_build_info(void) {
    snprintf(build_info_buffer, sizeof(build_info_buffer),
        "Build Date: %s %s\n"
        "Version Code: 0x%08X\n"
        "Description: %s",
        FIRMWARE_BUILD_DATE,
        FIRMWARE_BUILD_TIME,
        PROJECT_VERSION_NUMBER,
        PROJECT_DESCRIPTION
    );
    return build_info_buffer;
}