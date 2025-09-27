/**
 * 📨 MQTT MESSAGE BUILDER MODULE IMPLEMENTATION
 * 
 * Builds JSON messages for GPS tracking, battery status, and system info
 * Separate module for easy debugging and testing
 */

#include "mqtt_message_builder.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "MQTT_MSG_BUILDER";

// 📨 INTERNAL STATE 📨

static struct {
    uint32_t messages_built;
    uint32_t build_errors;
    size_t last_message_size;
    char last_error[128];
    mqtt_message_options_t last_options;
} g_builder_stats = {0};

// 📨 FORWARD DECLARATIONS 📨

static bool validate_coordinates_impl(double lat, double lon);
static void update_build_stats(bool success, size_t message_size, const char* error);
static const char* get_fix_quality_string(uint8_t quality);

// 📨 MAIN MESSAGE BUILDERS 📨

static bool mqtt_build_tracking_message_impl(
    const mqtt_gps_data_t* gps_data,
    const mqtt_battery_data_t* battery_data,
    const mqtt_system_data_t* system_data,
    const mqtt_message_options_t* options,
    char* message_buffer,
    size_t buffer_size,
    mqtt_message_result_t* result)
{
    if (!message_buffer || !options || !result) {
        ESP_LOGE(TAG, "❌ Invalid parameters");
        return false;
    }
    
    // Initialize result
    memset(result, 0, sizeof(mqtt_message_result_t));
    
    // Store options for debugging
    memcpy(&g_builder_stats.last_options, options, sizeof(mqtt_message_options_t));
    
    // Start JSON object
    size_t pos = 0;
    int written;
    
    if (options->pretty_format) {
        written = snprintf(message_buffer + pos, buffer_size - pos, "{\n");
    } else {
        written = snprintf(message_buffer + pos, buffer_size - pos, "{");
    }
    
    if (written < 0 || pos + written >= buffer_size) {
        update_build_stats(false, 0, "Buffer too small for header");
        strncpy(result->error_message, "Buffer too small", sizeof(result->error_message) - 1);
        return false;
    }
    pos += written;
    
    // Add timestamp if requested
    if (options->include_timestamp) {
        char timestamp_str[32];
        if (mqtt_get_timestamp_string_impl(timestamp_str, sizeof(timestamp_str))) {
            const char* format = options->pretty_format ? 
                "  \"timestamp\": \"%s\",\n" : "\"timestamp\":\"%s\",";
            written = snprintf(message_buffer + pos, buffer_size - pos, format, timestamp_str);
            if (written > 0 && pos + written < buffer_size) {
                pos += written;
            }
        }
    }
    
    // Add GPS data if provided and requested
    if (gps_data && options->include_gps_data) {
        // Validate coordinates if requested
        if (options->validate_coordinates && !validate_coordinates_impl(gps_data->latitude, gps_data->longitude)) {
            update_build_stats(false, 0, "Invalid GPS coordinates");
            strncpy(result->error_message, "Invalid GPS coordinates", sizeof(result->error_message) - 1);
            return false;
        }
        
        if (options->pretty_format) {
            written = snprintf(message_buffer + pos, buffer_size - pos,
                "  \"gps\": {\n"
                "    \"fix\": %s,\n"
                "    \"latitude\": %.8f,\n" 
                "    \"longitude\": %.8f,\n"
                "    \"altitude\": %.1f,\n"
                "    \"satellites\": %d,\n"
                "    \"quality\": \"%s\",\n"
                "    \"hdop\": %.2f\n"
                "  }",
                gps_data->has_valid_fix ? "true" : "false",
                gps_data->latitude,
                gps_data->longitude,
                gps_data->altitude,
                gps_data->satellites_used,
                get_fix_quality_string(gps_data->fix_quality),
                gps_data->hdop);
        } else {
            written = snprintf(message_buffer + pos, buffer_size - pos,
                "\"gps\":{\"fix\":%s,\"lat\":%.8f,\"lon\":%.8f,\"alt\":%.1f,\"sat\":%d,\"qual\":\"%s\",\"hdop\":%.2f}",
                gps_data->has_valid_fix ? "true" : "false",
                gps_data->latitude,
                gps_data->longitude, 
                gps_data->altitude,
                gps_data->satellites_used,
                get_fix_quality_string(gps_data->fix_quality),
                gps_data->hdop);
        }
        
        if (written < 0 || pos + written >= buffer_size) {
            update_build_stats(false, pos, "Buffer too small for GPS data");
            strncpy(result->error_message, "Buffer too small for GPS", sizeof(result->error_message) - 1);
            return false;
        }
        pos += written;
        result->gps_included = true;
        
        // Add comma if more data follows
        if ((battery_data && options->include_battery_data) || (system_data && options->include_system_data)) {
            written = snprintf(message_buffer + pos, buffer_size - pos, 
                               options->pretty_format ? ",\n" : ",");
            if (written > 0 && pos + written < buffer_size) {
                pos += written;
            }
        }
    }
    
    // Add battery data if provided and requested
    if (battery_data && options->include_battery_data) {
        if (options->pretty_format) {
            written = snprintf(message_buffer + pos, buffer_size - pos,
                "  \"battery\": {\n"
                "    \"voltage\": %.2f,\n"
                "    \"percentage\": %.1f,\n"
                "    \"charging\": %s,\n"
                "    \"critical\": %s\n"
                "  }",
                battery_data->voltage,
                battery_data->percentage,
                battery_data->is_charging ? "true" : "false",
                battery_data->is_critical ? "true" : "false");
        } else {
            written = snprintf(message_buffer + pos, buffer_size - pos,
                "\"battery\":{\"voltage\":%.2f,\"percentage\":%.1f,\"charging\":%s,\"critical\":%s}",
                battery_data->voltage,
                battery_data->percentage,
                battery_data->is_charging ? "true" : "false",
                battery_data->is_critical ? "true" : "false");
        }
        
        if (written < 0 || pos + written >= buffer_size) {
            update_build_stats(false, pos, "Buffer too small for battery data");
            strncpy(result->error_message, "Buffer too small for battery", sizeof(result->error_message) - 1);
            return false;
        }
        pos += written;
        result->battery_included = true;
        
        // Add comma if system data follows
        if (system_data && options->include_system_data) {
            written = snprintf(message_buffer + pos, buffer_size - pos,
                               options->pretty_format ? ",\n" : ",");
            if (written > 0 && pos + written < buffer_size) {
                pos += written;
            }
        }
    }
    
    // Add system data if provided and requested
    if (system_data && options->include_system_data) {
        if (options->pretty_format) {
            written = snprintf(message_buffer + pos, buffer_size - pos,
                "  \"system\": {\n"
                "    \"uptime\": %u,\n"
                "    \"free_heap\": %u,\n"
                "    \"rssi\": %d,\n"
                "    \"firmware\": \"%s\",\n"
                "    \"device_id\": \"%s\"\n"
                "  }",
                system_data->uptime_seconds,
                system_data->free_heap,
                system_data->rssi,
                system_data->firmware_version ? system_data->firmware_version : "unknown",
                system_data->device_id ? system_data->device_id : "esp32_gps_tracker");
        } else {
            written = snprintf(message_buffer + pos, buffer_size - pos,
                "\"system\":{\"uptime\":%u,\"heap\":%u,\"rssi\":%d,\"fw\":\"%s\",\"id\":\"%s\"}",
                system_data->uptime_seconds,
                system_data->free_heap,
                system_data->rssi,
                system_data->firmware_version ? system_data->firmware_version : "unknown", 
                system_data->device_id ? system_data->device_id : "esp32_gps_tracker");
        }
        
        if (written < 0 || pos + written >= buffer_size) {
            update_build_stats(false, pos, "Buffer too small for system data");
            strncpy(result->error_message, "Buffer too small for system", sizeof(result->error_message) - 1);
            return false;
        }
        pos += written;
        result->system_included = true;
    }
    
    // Close JSON object
    written = snprintf(message_buffer + pos, buffer_size - pos, 
                      options->pretty_format ? "\n}" : "}");
    if (written < 0 || pos + written >= buffer_size) {
        update_build_stats(false, pos, "Buffer too small for footer");
        strncpy(result->error_message, "Buffer too small for footer", sizeof(result->error_message) - 1);
        return false;
    }
    pos += written;
    
    // Success
    result->success = true;
    result->message_length = pos;
    update_build_stats(true, pos, NULL);
    
    ESP_LOGD(TAG, "✅ Built tracking message: %zu bytes", pos);
    if (options->pretty_format) {
        ESP_LOGV(TAG, "📨 Message:\n%s", message_buffer);
    }
    
    return true;
}

// 📨 SIMPLIFIED MESSAGE BUILDERS 📨

static bool mqtt_build_gps_message_impl(
    const mqtt_gps_data_t* gps_data,
    char* message_buffer,
    size_t buffer_size,
    mqtt_message_result_t* result)
{
    mqtt_message_options_t options = {
        .include_gps_data = true,
        .include_battery_data = false,
        .include_system_data = false,
        .pretty_format = false,
        .include_timestamp = true,
        .validate_coordinates = true,
        .max_message_size = buffer_size
    };
    
    return mqtt_build_tracking_message_impl(gps_data, NULL, NULL, &options, message_buffer, buffer_size, result);
}

static bool mqtt_build_battery_message_impl(
    const mqtt_battery_data_t* battery_data,
    char* message_buffer,
    size_t buffer_size,
    mqtt_message_result_t* result)
{
    mqtt_message_options_t options = {
        .include_gps_data = false,
        .include_battery_data = true,
        .include_system_data = false,
        .pretty_format = false,
        .include_timestamp = true,
        .validate_coordinates = false,
        .max_message_size = buffer_size
    };
    
    return mqtt_build_tracking_message_impl(NULL, battery_data, NULL, &options, message_buffer, buffer_size, result);
}

static bool mqtt_build_system_message_impl(
    const mqtt_system_data_t* system_data,
    char* message_buffer,
    size_t buffer_size,
    mqtt_message_result_t* result)
{
    mqtt_message_options_t options = {
        .include_gps_data = false,
        .include_battery_data = false,
        .include_system_data = true,
        .pretty_format = false,
        .include_timestamp = true,
        .validate_coordinates = false,
        .max_message_size = buffer_size
    };
    
    return mqtt_build_tracking_message_impl(NULL, NULL, system_data, &options, message_buffer, buffer_size, result);
}

// 📨 UTILITY FUNCTIONS 📨

static bool validate_coordinates_impl(double lat, double lon)
{
    return (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0);
}

static size_t mqtt_estimate_message_size_impl(const mqtt_message_options_t* options)
{
    size_t size = 20; // Base JSON overhead
    
    if (options->include_timestamp) size += 30;
    if (options->include_gps_data) size += options->pretty_format ? 200 : 150;
    if (options->include_battery_data) size += options->pretty_format ? 120 : 80;
    if (options->include_system_data) size += options->pretty_format ? 150 : 100;
    
    return size;
}

static bool mqtt_get_timestamp_string_impl(char* timestamp_buffer, size_t buffer_size)
{
    if (!timestamp_buffer || buffer_size < 20) {
        return false;
    }
    
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        // Fallback to system tick count
        uint32_t ticks = xTaskGetTickCount() * portTICK_PERIOD_MS;
        snprintf(timestamp_buffer, buffer_size, "%u", ticks);
        return true;
    }
    
    struct tm* timeinfo = gmtime(&tv.tv_sec);
    if (!timeinfo) {
        return false;
    }
    
    snprintf(timestamp_buffer, buffer_size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
    
    return true;
}

static bool mqtt_escape_json_string_impl(const char* input, char* output, size_t output_size)
{
    if (!input || !output || output_size < 2) {
        return false;
    }
    
    size_t input_len = strlen(input);
    size_t out_pos = 0;
    
    for (size_t i = 0; i < input_len && out_pos < output_size - 1; i++) {
        char c = input[i];
        
        switch (c) {
            case '"':
                if (out_pos + 2 < output_size) {
                    output[out_pos++] = '\\';
                    output[out_pos++] = '"';
                }
                break;
            case '\\':
                if (out_pos + 2 < output_size) {
                    output[out_pos++] = '\\';
                    output[out_pos++] = '\\';
                }
                break;
            case '\n':
                if (out_pos + 2 < output_size) {
                    output[out_pos++] = '\\';
                    output[out_pos++] = 'n';
                }
                break;
            case '\r':
                if (out_pos + 2 < output_size) {
                    output[out_pos++] = '\\';
                    output[out_pos++] = 'r';
                }
                break;
            case '\t':
                if (out_pos + 2 < output_size) {
                    output[out_pos++] = '\\';
                    output[out_pos++] = 't';
                }
                break;
            default:
                output[out_pos++] = c;
                break;
        }
    }
    
    output[out_pos] = '\0';
    return true;
}

static void mqtt_get_debug_info_impl(char* debug_str, size_t max_len)
{
    if (!debug_str) return;
    
    snprintf(debug_str, max_len,
        "MSG_BUILDER: built=%u, errors=%u, last_size=%zu, gps=%s, battery=%s, system=%s",
        g_builder_stats.messages_built,
        g_builder_stats.build_errors,
        g_builder_stats.last_message_size,
        g_builder_stats.last_options.include_gps_data ? "Y" : "N",
        g_builder_stats.last_options.include_battery_data ? "Y" : "N", 
        g_builder_stats.last_options.include_system_data ? "Y" : "N");
}

// 📨 HELPER FUNCTIONS 📨

static void update_build_stats(bool success, size_t message_size, const char* error)
{
    if (success) {
        g_builder_stats.messages_built++;
        g_builder_stats.last_message_size = message_size;
        memset(g_builder_stats.last_error, 0, sizeof(g_builder_stats.last_error));
    } else {
        g_builder_stats.build_errors++;
        if (error) {
            strncpy(g_builder_stats.last_error, error, sizeof(g_builder_stats.last_error) - 1);
        }
    }
}

static const char* get_fix_quality_string(uint8_t quality)
{
    switch (quality) {
        case 0: return "invalid";
        case 1: return "gps";
        case 2: return "dgps";
        case 3: return "pps";
        case 4: return "rtk";
        case 5: return "float_rtk";
        case 6: return "estimated";
        case 7: return "manual";
        case 8: return "simulation";
        default: return "unknown";
    }
}

// 📨 TOPIC HELPER FUNCTIONS 📨

bool mqtt_message_builder_get_gps_topic(const char* device_id, char* topic_buffer, size_t buffer_size)
{
    if (!topic_buffer || buffer_size < 20) return false;
    
    const char* id = device_id ? device_id : "esp32_tracker";
    return snprintf(topic_buffer, buffer_size, "gps_tracker/%s/location", id) < buffer_size;
}

bool mqtt_message_builder_get_battery_topic(const char* device_id, char* topic_buffer, size_t buffer_size)
{
    if (!topic_buffer || buffer_size < 20) return false;
    
    const char* id = device_id ? device_id : "esp32_tracker";
    return snprintf(topic_buffer, buffer_size, "gps_tracker/%s/battery", id) < buffer_size;
}

bool mqtt_message_builder_get_system_topic(const char* device_id, char* topic_buffer, size_t buffer_size)
{
    if (!topic_buffer || buffer_size < 20) return false;
    
    const char* id = device_id ? device_id : "esp32_tracker";
    return snprintf(topic_buffer, buffer_size, "gps_tracker/%s/system", id) < buffer_size;
}

// 📨 INTERFACE STRUCTURE 📨

static const mqtt_message_builder_interface_t mqtt_message_builder_interface = {
    .build_tracking_message = mqtt_build_tracking_message_impl,
    .build_gps_message = mqtt_build_gps_message_impl,
    .build_battery_message = mqtt_build_battery_message_impl,
    .build_system_message = mqtt_build_system_message_impl,
    .validate_gps_coordinates = validate_coordinates_impl,
    .estimate_message_size = mqtt_estimate_message_size_impl,
    .get_timestamp_string = mqtt_get_timestamp_string_impl,
    .escape_json_string = mqtt_escape_json_string_impl,
    .get_debug_info = mqtt_get_debug_info_impl,
};

const mqtt_message_builder_interface_t* mqtt_message_builder_get_interface(void)
{
    return &mqtt_message_builder_interface;
}

// 📨 DEFAULT OPTIONS 📨

mqtt_message_options_t mqtt_message_builder_get_default_options(void)
{
    mqtt_message_options_t options = {
        .include_gps_data = true,
        .include_battery_data = true,
        .include_system_data = false,
        .pretty_format = false,
        .include_timestamp = true,
        .validate_coordinates = true,
        .max_message_size = 1024,
    };
    
    return options;
}