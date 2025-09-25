#include "modem_init.h"
#include "../../config.h"
#include "../lte/lte_module.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "MODEM_INIT";

// Static interface instance
static modem_init_interface_t s_modem_interface;
static bool s_initialized = false;

// Forward declarations
static bool test_modem_ready_impl(void);
static modem_status_t get_modem_status_impl(void);
static bool wait_for_network_impl(int timeout_seconds);
static bool test_connectivity_impl(const char* host, network_test_result_t* result);
static bool ping_google_impl(network_test_result_t* result);
static bool initialize_gps_impl(void);
static bool start_gps_polling_impl(void);
static bool get_gps_fix_impl(gps_fix_info_t* fix_info);
static bool wait_for_gps_fix_impl(int timeout_seconds, gps_fix_info_t* fix_info);
static void print_status_impl(void);
static void reset_modem_impl(void);

/**
 * @brief Test basic modem readiness with AT commands
 * Following Waveshare SIM7670G startup sequence
 */
static bool test_modem_ready_impl(void)
{
    ESP_LOGI(TAG, "🔧 Testing modem readiness...");
    
    // Get LTE interface for AT commands
    lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        ESP_LOGE(TAG, "❌ Failed to get LTE interface");
        return false;
    }
    
    // CRITICAL: Power off GNSS first to prevent automatic NMEA output during modem startup
    ESP_LOGI(TAG, "� Disabling GPS first to prevent AT command interference...");
    at_response_t response = {0};
    lte->send_at_command("AT+CGNSSPWR=0", &response, 3000); // Don't fail if this errors
    
    // Test basic AT command
    ESP_LOGI(TAG, "📡 Testing basic AT communication...");
    bool success = lte->send_at_command("AT", &response, 3000);
    
    if (!success || !response.success) {
        ESP_LOGE(TAG, "❌ Modem not responding to AT commands");
        ESP_LOGE(TAG, "   Response: '%s'", response.response);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Modem responding to AT commands");
    
    // Check SIM card status
    ESP_LOGI(TAG, "📱 Checking SIM card status...");
    success = lte->send_at_command("AT+CPIN?", &response, 5000);
    
    if (!success || !response.success) {
        ESP_LOGE(TAG, "❌ Failed to check SIM card status");
        return false;
    }
    
    if (strstr(response.response, "READY")) {
        ESP_LOGI(TAG, "✅ SIM card ready");
    } else {
        ESP_LOGW(TAG, "⚠️  SIM card status: %s", response.response);
    }
    
    // Check signal strength
    ESP_LOGI(TAG, "📶 Checking signal strength...");
    success = lte->send_at_command("AT+CSQ", &response, 3000);
    
    if (success && response.success) {
        ESP_LOGI(TAG, "📊 Signal quality: %s", response.response);
    }
    
    // Check network registration
    ESP_LOGI(TAG, "🌐 Checking network registration...");
    success = lte->send_at_command("AT+CREG?", &response, 3000);
    
    if (success && response.success) {
        ESP_LOGI(TAG, "🔗 Network registration: %s", response.response);
        
        // Check if registered (status 1 or 5)
        if (strstr(response.response, ",1") || strstr(response.response, ",5")) {
            ESP_LOGI(TAG, "✅ Network registered");
            return true;
        }
    }
    
    ESP_LOGW(TAG, "⚠️  Network not yet registered, but modem is ready");
    return true; // Modem is ready even if not registered yet
}

/**
 * @brief Get detailed modem status
 */
static modem_status_t get_modem_status_impl(void)
{
    lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        return MODEM_STATUS_FAILED;
    }
    
    at_response_t response = {0};
    
    // Test basic communication
    if (!lte->send_at_command("AT", &response, 3000) || !response.success) {
        return MODEM_STATUS_FAILED;
    }
    
    // Check SIM status
    if (lte->send_at_command("AT+CPIN?", &response, 3000) && response.success) {
        if (!strstr(response.response, "READY")) {
            return MODEM_STATUS_READY; // Modem ready but SIM not ready
        }
    }
    
    // Check network registration
    if (lte->send_at_command("AT+CREG?", &response, 3000) && response.success) {
        if (strstr(response.response, ",1") || strstr(response.response, ",5")) {
            return MODEM_STATUS_NETWORK_REGISTERED;
        }
    }
    
    return MODEM_STATUS_SIM_READY;
}

/**
 * @brief Wait for network registration
 */
static bool wait_for_network_impl(int timeout_seconds)
{
    ESP_LOGI(TAG, "⏳ Waiting for network registration (timeout: %d seconds)...", timeout_seconds);
    
    lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        return false;
    }
    
    int elapsed = 0;
    at_response_t response = {0};
    
    while (elapsed < timeout_seconds) {
        // Check network registration status
        if (lte->send_at_command("AT+CREG?", &response, 3000) && response.success) {
            if (strstr(response.response, ",1") || strstr(response.response, ",5")) {
                ESP_LOGI(TAG, "✅ Network registered after %d seconds", elapsed);
                return true;
            }
        }
        
        // Show progress every 5 seconds
        if (elapsed % 5 == 0) {
            ESP_LOGI(TAG, "   Still waiting... (%d/%d seconds)", elapsed, timeout_seconds);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        elapsed++;
    }
    
    ESP_LOGW(TAG, "⚠️  Network registration timeout after %d seconds", timeout_seconds);
    return false;
}

/**
 * @brief Test network connectivity by pinging a host
 */
static bool test_connectivity_impl(const char* host, network_test_result_t* result)
{
    if (!result) {
        return false;
    }
    
    // Initialize result
    result->ping_success = false;
    result->response_time_ms = -1;
    strcpy(result->error_message, "Unknown error");
    
    ESP_LOGI(TAG, "🌐 Testing connectivity to %s...", host);
    
    lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        strcpy(result->error_message, "LTE interface not available");
        return false;
    }
    
    // Enable network PDP context first
    ESP_LOGI(TAG, "📱 Activating PDP context...");
    at_response_t response = {0};
    
    // Set APN (using default m2mglobal)
    char apn_cmd[128];
    snprintf(apn_cmd, sizeof(apn_cmd), "AT+CGDCONT=1,\"IP\",\"m2mglobal\"");
    
    if (!lte->send_at_command(apn_cmd, &response, 5000) || !response.success) {
        strcpy(result->error_message, "Failed to set APN");
        return false;
    }
    
    // Activate PDP context
    if (!lte->send_at_command("AT+CGACT=1,1", &response, 10000) || !response.success) {
        strcpy(result->error_message, "Failed to activate PDP context");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ PDP context activated");
    
    // Ping command
    char ping_cmd[256];
    snprintf(ping_cmd, sizeof(ping_cmd), "AT+CPING=\"%s\",1,4,64,1000", host);
    
    ESP_LOGI(TAG, "🏓 Sending ping command...");
    uint32_t ping_start = xTaskGetTickCount();
    
    if (lte->send_at_command(ping_cmd, &response, 15000) && response.success) {
        uint32_t ping_end = xTaskGetTickCount();
        result->response_time_ms = (ping_end - ping_start) * portTICK_PERIOD_MS;
        
        // Check for successful ping response
        if (strstr(response.response, "+CPING:") && 
            (strstr(response.response, "OK") || strstr(response.response, "64"))) {
            result->ping_success = true;
            strcpy(result->error_message, "Success");
            ESP_LOGI(TAG, "✅ Ping successful in %d ms", result->response_time_ms);
            return true;
        }
    }
    
    strcpy(result->error_message, "Ping timeout or failure");
    ESP_LOGW(TAG, "❌ Ping failed: %s", response.response);
    return false;
}

/**
 * @brief Test connectivity to Google
 */
static bool ping_google_impl(network_test_result_t* result)
{
    return test_connectivity_impl("8.8.8.8", result);
}

/**
 * @brief Initialize GPS with proper SIM7670G sequence
 */
static bool initialize_gps_impl(void)
{
    ESP_LOGI(TAG, "🛰️ Initializing GPS (SIM7670G GNSS)...");
    
    lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        ESP_LOGE(TAG, "❌ LTE interface not available for GPS");
        return false;
    }
    
    at_response_t response = {0};
    
    // Step 1: CRITICAL - Switch GNSS to UART port FIRST (before enabling data output)
    // This prevents NMEA data from interfering with AT commands
    ESP_LOGI(TAG, "🔀 Switching GNSS to dedicated UART port (FIRST - prevent AT interference)...");
    if (!lte->send_at_command("AT+CGNSSPORTSWITCH=0,1", &response, 3000)) {
        ESP_LOGE(TAG, "❌ Failed to switch GNSS port");
        return false;
    }
    
    if (response.success) {
        ESP_LOGI(TAG, "✅ GNSS port switched to UART - AT commands now safe");
    } else {
        ESP_LOGW(TAG, "⚠️  GNSS port switch response: %s", response.response);
    }
    
    // Step 2: Power on GNSS (as per Waveshare documentation)
    ESP_LOGI(TAG, "🔌 Powering on GNSS module...");
    if (!lte->send_at_command("AT+CGNSSPWR=1", &response, 5000)) {
        ESP_LOGE(TAG, "❌ Failed to power on GNSS");
        return false;
    }
    
    // Check for READY response
    if (response.success && strstr(response.response, "READY")) {
        ESP_LOGI(TAG, "✅ GNSS powered on successfully");
    } else {
        ESP_LOGW(TAG, "⚠️  GNSS power response: %s", response.response);
    }
    
    // Step 3: Wait for GNSS to initialize
    ESP_LOGI(TAG, "⏳ Waiting for GNSS initialization...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Step 4: Enable GNSS data output (now safe - data goes to dedicated port)
    ESP_LOGI(TAG, "📡 Enabling GNSS data output...");
    if (!lte->send_at_command("AT+CGNSSTST=1", &response, 3000)) {
        ESP_LOGE(TAG, "❌ Failed to enable GNSS data output");
        return false;
    }
    
    if (response.success) {
        ESP_LOGI(TAG, "✅ GNSS data output enabled");
    } else {
        ESP_LOGW(TAG, "⚠️  GNSS data response: %s", response.response);
    }
    
    // Step 5: Wait for NMEA data stream to stabilize
    ESP_LOGI(TAG, "⏳ Waiting for NMEA data stream to initialize...");
    vTaskDelay(pdMS_TO_TICKS(3000)); // Give GPS time to start outputting NMEA data
    
    ESP_LOGI(TAG, "✅ GPS initialization complete");
    return true;
}

/**
 * @brief Start GPS polling
 */
static bool start_gps_polling_impl(void)
{
    ESP_LOGI(TAG, "🔄 Starting GPS polling...");
    
    lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        return false;
    }
    
    at_response_t response = {0};
    
    // Start continuous GNSS info reporting
    if (lte->send_at_command("AT+CGNSINF", &response, 3000) && response.success) {
        ESP_LOGI(TAG, "📊 GNSS info: %s", response.response);
        return true;
    }
    
    return false;
}

/**
 * @brief Read raw NMEA data from UART (after AT+CGNSSPORTSWITCH=0,1)
 * Following Waveshare Arduino example - no more AT commands after port switch!
 */
static bool read_nmea_data_from_uart(char* buffer, size_t buffer_size, int timeout_ms)
{
    if (!buffer || buffer_size == 0) {
        return false;
    }
    
    size_t bytes_read = 0;
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    while (bytes_read < (buffer_size - 1)) {
        // Check for timeout
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if ((current_time - start_time) > timeout_ms) {
            break;
        }
        
        // Try to read one byte from UART
        int len = uart_read_bytes(UART_NUM_1, &buffer[bytes_read], 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            bytes_read += len;
            // Look for complete NMEA sentence (ends with \r\n)
            if (bytes_read >= 2 && buffer[bytes_read-2] == '\r' && buffer[bytes_read-1] == '\n') {
                buffer[bytes_read] = '\0';
                return true;
            }
        }
    }
    
    buffer[bytes_read] = '\0';
    return bytes_read > 0;
}

/**
 * @brief Get current GPS fix information by reading raw NMEA data
 * Following Waveshare example: after port switch, read NMEA directly!
 */
static bool get_gps_fix_impl(gps_fix_info_t* fix_info)
{
    if (!fix_info) {
        return false;
    }
    
    // Initialize fix info
    memset(fix_info, 0, sizeof(gps_fix_info_t));
    fix_info->has_fix = false;
    
    ESP_LOGI(TAG, "📡 Reading raw NMEA data from UART (Waveshare approach)...");
    
    // Read NMEA data directly from UART (no AT commands after port switch!)
    char nmea_buffer[512];
    if (read_nmea_data_from_uart(nmea_buffer, sizeof(nmea_buffer), 3000)) {
        ESP_LOGI(TAG, "📍 Raw NMEA: %s", nmea_buffer);
        strncpy(fix_info->nmea_data, nmea_buffer, sizeof(fix_info->nmea_data) - 1);
        
        // Basic NMEA parsing - look for $GPRMC or $GNRMC (Recommended Minimum)
        if (strstr(nmea_buffer, "$GPRMC") || strstr(nmea_buffer, "$GNRMC")) {
            ESP_LOGI(TAG, "✅ Found GPRMC/GNRMC sentence");
            
            // Parse NMEA RMC sentence: $GPRMC,time,status,lat,latNS,lon,lonEW,speed,course,date,magvar,magvarEW,checksum
            // Status: A = Active (valid fix), V = Void (no fix)
            char* tokens[15];
            char* nmea_copy = strdup(nmea_buffer);
            char* token = strtok(nmea_copy, ",");
            int token_count = 0;
            
            while (token && token_count < 15) {
                tokens[token_count] = token;
                token = strtok(NULL, ",");
                token_count++;
            }
            
            // Check if we have a valid fix (status = 'A')
            if (token_count > 2 && tokens[2][0] == 'A') {
                fix_info->has_fix = true;
                
                // Parse latitude and longitude
                if (token_count > 5) {
                    fix_info->latitude = atof(tokens[3]) / 100.0;  // Convert DDMM.MMMM to decimal degrees
                    fix_info->longitude = atof(tokens[5]) / 100.0;
                    
                    // Apply hemisphere corrections
                    if (token_count > 4 && tokens[4][0] == 'S') fix_info->latitude = -fix_info->latitude;
                    if (token_count > 6 && tokens[6][0] == 'W') fix_info->longitude = -fix_info->longitude;
                }
                
                if (token_count > 1) strncpy(fix_info->fix_time, tokens[1], sizeof(fix_info->fix_time) - 1);
                ESP_LOGI(TAG, "🎯 GPS FIX FOUND! Lat: %.6f, Lon: %.6f", fix_info->latitude, fix_info->longitude);
            } else {
                ESP_LOGI(TAG, "📍 GPS data received but no fix yet (status: %c)", token_count > 2 ? tokens[2][0] : '?');
            }
            
            free(nmea_copy);
            return true;
        } else {
            ESP_LOGI(TAG, "📡 NMEA data received but not RMC sentence");
            return true;  // Still success, just different sentence type
        }
    }
    
    return false;
}

/**
 * @brief Wait for GPS fix with timeout
 */
static bool wait_for_gps_fix_impl(int timeout_seconds, gps_fix_info_t* fix_info)
{
    ESP_LOGI(TAG, "🛰️ Waiting for GPS fix (timeout: %d seconds)...", timeout_seconds);
    ESP_LOGI(TAG, "   📍 Ensure GPS antenna is connected and device is outdoors");
    
    int elapsed = 0;
    
    while (elapsed < timeout_seconds) {
        if (get_gps_fix_impl(fix_info) && fix_info->has_fix) {
            ESP_LOGI(TAG, "✅ GPS fix acquired after %d seconds!", elapsed);
            ESP_LOGI(TAG, "   📍 Position: %.6f, %.6f (altitude: %.1fm)", 
                     fix_info->latitude, fix_info->longitude, fix_info->altitude);
            ESP_LOGI(TAG, "   🛰️ Satellites: %d", fix_info->satellites_used);
            ESP_LOGI(TAG, "   ⏰ Fix time: %s", fix_info->fix_time);
            return true;
        }
        
        // Show progress every 10 seconds  
        if (elapsed % 10 == 0) {
            ESP_LOGI(TAG, "   🔍 Still searching for satellites... (%d/%d seconds)", elapsed, timeout_seconds);
            if (fix_info && fix_info->satellites_used > 0) {
                ESP_LOGI(TAG, "   🛰️ Satellites visible: %d", fix_info->satellites_used);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Slow down GPS polling to every 5 seconds (was 1 second)
        elapsed += 5;
    }
    
    ESP_LOGW(TAG, "⚠️  GPS fix timeout after %d seconds", timeout_seconds);
    ESP_LOGI(TAG, "   💡 Try moving to a location with better sky visibility");
    return false;
}

/**
 * @brief Print modem status
 */
static void print_status_impl(void)
{
    ESP_LOGI(TAG, "📊 === MODEM STATUS ===");
    
    modem_status_t status = get_modem_status_impl();
    
    switch (status) {
        case MODEM_STATUS_FAILED:
            ESP_LOGI(TAG, "   Status: ❌ Failed/Not Ready");
            break;
        case MODEM_STATUS_READY:
            ESP_LOGI(TAG, "   Status: 🟡 Ready (SIM not ready)");
            break;
        case MODEM_STATUS_SIM_READY:
            ESP_LOGI(TAG, "   Status: 🟠 SIM Ready (Network not registered)");
            break;
        case MODEM_STATUS_NETWORK_REGISTERED:
            ESP_LOGI(TAG, "   Status: ✅ Network Registered");
            break;
        default:
            ESP_LOGI(TAG, "   Status: ❓ Unknown");
            break;
    }
    
    // Test connectivity
    network_test_result_t ping_result = {0};
    if (ping_google_impl(&ping_result)) {
        ESP_LOGI(TAG, "   Internet: ✅ Connected (ping: %dms)", ping_result.response_time_ms);
    } else {
        ESP_LOGI(TAG, "   Internet: ❌ Not connected (%s)", ping_result.error_message);
    }
    
    // Test GPS
    gps_fix_info_t gps_info = {0};
    if (get_gps_fix_impl(&gps_info)) {
        if (gps_info.has_fix) {
            ESP_LOGI(TAG, "   GPS: ✅ Fixed (%.6f, %.6f, %d sats)", 
                     gps_info.latitude, gps_info.longitude, gps_info.satellites_used);
        } else {
            ESP_LOGI(TAG, "   GPS: 🟡 Active but no fix (%d sats visible)", gps_info.satellites_used);
        }
    } else {
        ESP_LOGI(TAG, "   GPS: ❌ Not active");
    }
    
    ESP_LOGI(TAG, "======================");
}

/**
 * @brief Reset modem (software reset)
 */
static void reset_modem_impl(void)
{
    ESP_LOGI(TAG, "🔄 Resetting modem...");
    
    lte_interface_t* lte = lte_get_interface();
    if (lte) {
        at_response_t response = {0};
        lte->send_at_command("AT+CFUN=1,1", &response, 10000);
        ESP_LOGI(TAG, "✅ Reset command sent");
        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for reset
    }
}

/**
 * @brief Create modem interface
 */
modem_init_interface_t* modem_init_create(void)
{
    if (s_initialized) {
        return &s_modem_interface;
    }
    
    // Initialize function pointers
    s_modem_interface.test_modem_ready = test_modem_ready_impl;
    s_modem_interface.get_modem_status = get_modem_status_impl;
    s_modem_interface.wait_for_network = wait_for_network_impl;
    s_modem_interface.test_connectivity = test_connectivity_impl;
    s_modem_interface.ping_google = ping_google_impl;
    s_modem_interface.initialize_gps = initialize_gps_impl;
    s_modem_interface.start_gps_polling = start_gps_polling_impl;
    s_modem_interface.get_gps_fix = get_gps_fix_impl;
    s_modem_interface.wait_for_gps_fix = wait_for_gps_fix_impl;
    s_modem_interface.print_status = print_status_impl;
    s_modem_interface.reset_modem = reset_modem_impl;
    
    s_initialized = true;
    ESP_LOGI(TAG, "✅ Modem initialization module created");
    
    return &s_modem_interface;
}

/**
 * @brief Destroy modem interface
 */
void modem_init_destroy(void)
{
    if (s_initialized) {
        memset(&s_modem_interface, 0, sizeof(s_modem_interface));
        s_initialized = false;
        ESP_LOGI(TAG, "✅ Modem initialization module destroyed");
    }
}

/**
 * @brief Complete modem initialization sequence
 * Implements Waveshare SIM7670G recommended startup procedure
 */
bool modem_init_complete_sequence(int timeout_seconds)
{
    ESP_LOGI(TAG, "🚀 === STARTING COMPLETE MODEM INITIALIZATION SEQUENCE ===");
    ESP_LOGI(TAG, "📖 Following Waveshare SIM7670G recommended procedure");
    
    modem_init_interface_t* modem = modem_init_create();
    if (!modem) {
        ESP_LOGE(TAG, "❌ Failed to create modem interface");
        return false;
    }
    
    // Step 1: Test modem readiness
    ESP_LOGI(TAG, "\n🔧 STEP 1: Testing modem readiness");
    if (!modem->test_modem_ready()) {
        ESP_LOGE(TAG, "❌ Modem readiness test failed");
        return false;
    }
    ESP_LOGI(TAG, "✅ STEP 1 COMPLETE: Modem is ready");
    
    // Step 2: Wait for network registration
    ESP_LOGI(TAG, "\n🌐 STEP 2: Waiting for network registration");
    if (!modem->wait_for_network(timeout_seconds / 2)) {
        ESP_LOGW(TAG, "⚠️  Network registration timeout, but continuing...");
    } else {
        ESP_LOGI(TAG, "✅ STEP 2 COMPLETE: Network registered");
    }
    
    // Step 3: Test connectivity
    ESP_LOGI(TAG, "\n🏓 STEP 3: Testing internet connectivity");
    network_test_result_t ping_result = {0};
    if (modem->ping_google(&ping_result)) {
        ESP_LOGI(TAG, "✅ STEP 3 COMPLETE: Internet connectivity confirmed (ping: %dms)", ping_result.response_time_ms);
    } else {
        ESP_LOGW(TAG, "⚠️  Internet connectivity test failed: %s", ping_result.error_message);
        ESP_LOGI(TAG, "   🔄 Continuing with GPS initialization anyway...");
    }
    
    // Step 4: Initialize GPS
    ESP_LOGI(TAG, "\n🛰️ STEP 4: Initializing GPS");
    if (!modem->initialize_gps()) {
        ESP_LOGE(TAG, "❌ GPS initialization failed");
        return false;
    }
    ESP_LOGI(TAG, "✅ STEP 4 COMPLETE: GPS initialized");
    
    // Step 5: Wait for GPS fix
    ESP_LOGI(TAG, "\n📍 STEP 5: Waiting for GPS fix");
    gps_fix_info_t gps_info = {0};
    if (modem->wait_for_gps_fix(timeout_seconds, &gps_info)) {
        ESP_LOGI(TAG, "✅ STEP 5 COMPLETE: GPS fix acquired!");
        ESP_LOGI(TAG, "   📍 Location: %.6f, %.6f (altitude: %.1fm)", 
                 gps_info.latitude, gps_info.longitude, gps_info.altitude);
        ESP_LOGI(TAG, "   🛰️ Satellites: %d", gps_info.satellites_used);
    } else {
        ESP_LOGW(TAG, "⚠️  GPS fix timeout, but GPS is active and searching");
    }
    
    // Final status
    ESP_LOGI(TAG, "\n📊 FINAL STATUS:");
    modem->print_status();
    
    ESP_LOGI(TAG, "\n🎉 === MODEM INITIALIZATION SEQUENCE COMPLETE ===");
    return true;
}
