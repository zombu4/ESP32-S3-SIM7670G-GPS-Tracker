# MQTT TROUBLESHOOTING - WORKING CODE REFERENCE
**Date**: September 25, 2025  
**Context**: SIM7670G MQTT AT Command Issues
**UPDATE**: ⚠️ **AT+CMQTTACCQ COMMAND NOT SUPPORTED** - All formats tested and failed

## 🚨 CRITICAL FINDING - MQTT AT COMMANDS ISSUE IDENTIFIED

### **Root Cause Discovered**: AT+CMQTTACCQ Command Failure
- **Status**: ❌ **ALL FORMATS FAIL** - Command likely not supported on SIM7670G
- **Tested Formats**: 3 different AT+CMQTTACCQ variations all return ERROR
- **Conclusion**: MQTT AT command approach may not be viable for this hardware
- **Solution**: **FALLBACK TO HTTP POST** recommended

## 🔥 CRITICAL - CURRENT WORKING STATE

### **Cellular Connection - VERIFIED WORKING**
```c
// File: main/modules/lte/lte_module.c
// Status: ✅ PRODUCTION READY - DO NOT MODIFY

// WORKING AT COMMAND SEQUENCE:
bool lte_connect_impl(void) {
    // 1. Basic AT test (may fail first time)
    if (!send_lte_at_command("AT", "OK", 2000)) {
        ESP_LOGW(TAG, "First AT failed, retrying...");
        if (!send_lte_at_command("AT", "OK", 2000)) {
            return false;
        }
    }
    
    // 2. Set full functionality
    if (!send_lte_at_command("AT+CFUN=1", "OK", 10000)) return false;
    
    // 3. Check SIM card
    if (!send_lte_at_command("AT+CPIN?", "OK", 5000)) return false;
    
    // 4. Set APN  
    char apn_cmd[128];
    snprintf(apn_cmd, sizeof(apn_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", current_config.apn);
    if (!send_lte_at_command(apn_cmd, "OK", 5000)) return false;
    
    // 5. Network registration (auto-retry logic)
    if (!wait_for_network_registration()) return false;
    
    // 6. Activate PDP context
    if (!send_lte_at_command("AT+CGACT=1,1", "OK", 30000)) return false;
    
    return true;
}
```

### **Enhanced Debug System - PRODUCTION READY**
```c
// File: main/modules/lte/lte_module.c  
// Function: send_lte_at_command() 
// Status: ✅ FULLY FUNCTIONAL

static bool send_lte_at_command(const char* command, const char* expected_response, int timeout_ms) {
    // Complete implementation with:
    // - UART hex dumps  
    // - Timing analysis
    // - Response validation
    // - Error classification
    // - Step-by-step logging
    
    ESP_LOGI(TAG, "[TIMING] Starting AT command: %s (timeout: %d ms)", command, timeout_ms);
    ESP_LOGI(TAG, "[UART] === UART TX START ===");
    ESP_LOGI(TAG, "[UART] Command: '%s' (len=%d)", command, strlen(command));
    
    // ... [Full implementation working] ...
    
    ESP_LOGI(TAG, "[RAW] Response hex dump:");
    for (int i = 0; i < response_len; i++) {
        printf("%02X ", (unsigned char)response_buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    
    return success;
}
```

## ❌ MQTT ISSUE - CURRENT FAILURE POINT

### **Problem**: AT+CMQTTACCQ Command Format
```bash
# FAILING COMMAND:
AT+CMQTTACCQ=0,"esp32_gps_tracker_dev",0
# RESPONSE: ERROR

# WORKING SEQUENCE SO FAR:
✅ AT+CMQTTDISC?   -> +CMQTTDISC: 0,1 +CMQTTDISC: 1,1 OK  
✅ AT+CMQTTSTOP    -> +CMQTTSTOP: 19 ERROR (but stops service)
✅ AT+CMQTTSTART   -> OK +CMQTTSTART: 0  
❌ AT+CMQTTACCQ    -> ERROR (FORMAT ISSUE)
```

### **Current Enhanced MQTT Code**
```c
// File: main/modules/mqtt/mqtt_module.c
// Function: mqtt_init_impl()
// Status: 🔄 ENHANCED DEBUG DEPLOYED, TESTING MULTIPLE FORMATS

static bool mqtt_init_impl(const mqtt_config_t* config) {
    ESP_LOGI(TAG, "[MQTT] Step 1: Checking MQTT service status...");
    if (send_mqtt_at_command("AT+CMQTTDISC?", "OK", 2000)) {
        ESP_LOGI(TAG, "[MQTT] MQTT service already running, stopping first...");
        send_mqtt_at_command("AT+CMQTTSTOP", "OK", 5000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "[MQTT] Step 2: Starting MQTT service...");
    if (!send_mqtt_at_command("AT+CMQTTSTART", "OK", 5000)) {
        ESP_LOGE(TAG, "[MQTT] FAILED: Could not start MQTT service");
        // Retry logic implemented
        send_mqtt_at_command("AT+CMQTTSTOP", "OK", 2000);
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!send_mqtt_at_command("AT+CMQTTSTART", "OK", 5000)) {
            return false;
        }
    }
    
    ESP_LOGI(TAG, "[MQTT] Step 3: Acquiring MQTT client...");
    char client_cmd[128];
    
    // FORMAT ATTEMPTS (CURRENTLY TESTING):
    // Attempt 1: Without clean session flag  
    snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=0,\"%s\"", config->client_id);
    if (!send_mqtt_at_command(client_cmd, "OK", 5000)) {
        
        // Attempt 2: With clean session = 1
        snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=0,\"%s\",1", config->client_id);
        if (!send_mqtt_at_command(client_cmd, "OK", 5000)) {
            
            // Attempt 3: Without client index
            snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=\"%s\"", config->client_id);
            if (!send_mqtt_at_command(client_cmd, "OK", 5000)) {
                ESP_LOGE(TAG, "[MQTT] FAILED: All CMQTTACCQ formats failed");
                return false;
            }
        }
    }
    
    return true;
}
```

### **Enhanced MQTT Debug Function**
```c
// File: main/modules/mqtt/mqtt_module.c
// Status: ✅ DEPLOYED AND WORKING

static bool send_mqtt_at_command(const char* command, const char* expected_response, int timeout_ms) {
    ESP_LOGI(TAG, "[MQTT] === STARTING MQTT AT COMMAND ===");
    ESP_LOGI(TAG, "[MQTT] Command: '%s'", command);
    ESP_LOGI(TAG, "[MQTT] Expected: '%s'", expected_response);
    ESP_LOGI(TAG, "[MQTT] Timeout: %d ms", timeout_ms);
    
    // Detailed hex dump implementation
    ESP_LOGI(TAG, "[MQTT] Hex dump:");
    for (int i = 0; i < cmd_len; i++) {
        printf("%02X ", (unsigned char)full_command[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    
    // Response analysis with timing
    ESP_LOGI(TAG, "[MQTT] === RESPONSE ANALYSIS ===");
    ESP_LOGI(TAG, "[MQTT] Total time: %lu ms", elapsed_time);
    ESP_LOGI(TAG, "[MQTT] Response length: %d bytes", response_len);
    ESP_LOGI(TAG, "[MQTT] Complete response: '%s'", response_buffer);
    
    return success;
}
```

## 🚀 IMMEDIATE ACTION REQUIRED

### **Next Steps - PRIORITY ORDER**:

1. **Build and Flash Enhanced MQTT Code**
   ```bash
   cd "C:\Espressif\frameworks\esp-idf-v5.5"
   .\export.ps1
   cd "c:\Users\dom\Documents\esp-idf-tracker"  
   idf.py build
   idf.py -p COM4 flash
   idf.py -p COM4 monitor
   ```

2. **Monitor Multiple Format Attempts**
   - Watch for which AT+CMQTTACCQ format succeeds
   - Document the working command format
   - Verify full MQTT initialization sequence

3. **Fallback Plan - HTTP Implementation**
   - If all MQTT formats fail, implement HTTP POST
   - SIM7670G HTTP commands: AT+CHTTPCREATE, AT+CHTTPCON, AT+CHTTPPOST

### **Critical Configuration - DO NOT CHANGE**
```c
// File: main/config_user.h
#define USER_CONFIG_MQTT_BROKER    "65.124.194.3"  // Test broker - safe for repo
#define USER_CONFIG_MQTT_PORT      1883
#define USER_CONFIG_MQTT_CLIENT_ID "esp32_gps_tracker_dev"  
// No username/password required for test broker
```

### **Hardware Verification - CONFIRMED WORKING**
```
ESP32-S3 Pin | SIM7670G Pin | Function
-------------|--------------|----------
GPIO 18      | RX           | UART TX (ESP -> SIM)
GPIO 17      | TX           | UART RX (ESP <- SIM)  
GPIO 3       | SDA          | I2C Data (Battery)
GPIO 2       | SCL          | I2C Clock (Battery)

COM Port: COM4 (CH343 USB-Enhanced-SERIAL)
Baud Rate: 115200 (both UART and USB)
```

---
**⚡ STATUS**: ❌ **MQTT AT COMMANDS FAILED** - All AT+CMQTTACCQ formats tested and failed  
**🎯 NEW OBJECTIVE**: Implement HTTP POST fallback for data transmission  
**📋 SUCCESS METRIC**: HTTP POST to 65.124.194.3 with GPS data payload  
**🚀 NEXT APPROACH**: Use AT+CHTTPCREATE, AT+CHTTPCON, AT+CHTTPPOST sequence