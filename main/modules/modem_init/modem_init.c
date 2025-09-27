#include "modem_init.h"
#include "../../config.h"
#include "../lte/lte_module.h"
#include "../apn/apn_manager.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
 ESP_LOGI(TAG, "Testing modem readiness...");
 
 // Get LTE interface for AT commands
 const lte_interface_t* lte = lte_get_interface();
 if (!lte) {
 ESP_LOGE(TAG, "Failed to get LTE interface");
 return false;
 }
 
 // CRITICAL: Power off GNSS first to prevent automatic NMEA output during modem startup
 ESP_LOGI(TAG, "Disabling GPS first to prevent AT command interference...");
 at_response_t response = {0};
 lte->send_at_command("AT+CGNSSPWR=0", &response, 3000); // Don't fail if this errors
 
 // Test basic AT command
 ESP_LOGI(TAG, "Testing basic AT communication...");
 bool success = lte->send_at_command("AT", &response, 3000);
 
 if (!success || !response.success) {
 ESP_LOGE(TAG, "Modem not responding to AT commands");
 ESP_LOGE(TAG, " Response: '%s'", response.response);
 return false;
 }
 
 ESP_LOGI(TAG, "Modem responding to AT commands");
 
 // Check SIM card status
 ESP_LOGI(TAG, "Checking SIM card status...");
 success = lte->send_at_command("AT+CPIN?", &response, 5000);
 
 if (!success || !response.success) {
 ESP_LOGE(TAG, "Failed to check SIM card status");
 return false;
 }
 
 if (strstr(response.response, "READY")) {
 ESP_LOGI(TAG, "SIM card ready");
 } else {
 ESP_LOGW(TAG, "SIM card status: %s", response.response);
 }
 
 // Check signal strength
 ESP_LOGI(TAG, "Checking signal strength...");
 success = lte->send_at_command("AT+CSQ", &response, 3000);
 
 if (success && response.success) {
 ESP_LOGI(TAG, "Signal quality: %s", response.response);
 }
 
 // Check network registration
 ESP_LOGI(TAG, "Checking network registration...");
 success = lte->send_at_command("AT+CREG?", &response, 3000);
 
 if (success && response.success) {
 ESP_LOGI(TAG, "Network registration: %s", response.response);
 
 // Check if registered (status 1 or 5)
 if (strstr(response.response, ",1") || strstr(response.response, ",5")) {
 ESP_LOGI(TAG, "Network registered");
 return true;
 }
 }
 
 ESP_LOGW(TAG, "Network not yet registered, but modem is ready");
 return true; // Modem is ready even if not registered yet
}

/**
 * @brief Get detailed modem status
 */
static modem_status_t get_modem_status_impl(void)
{
 const lte_interface_t* lte = lte_get_interface();
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
 ESP_LOGI(TAG, " Waiting for network registration (timeout: %d seconds)...", timeout_seconds);
 
 const lte_interface_t* lte = lte_get_interface();
 if (!lte) {
 return false;
 }
 
 int elapsed = 0;
 at_response_t response = {0};
 
 while (elapsed < timeout_seconds) {
 // Check network registration status
 if (lte->send_at_command("AT+CREG?", &response, 3000) && response.success) {
 if (strstr(response.response, ",1") || strstr(response.response, ",5")) {
 ESP_LOGI(TAG, "Network registered after %d seconds", elapsed);
 return true;
 }
 }
 
 // Show progress every 5 seconds
 if (elapsed % 5 == 0) {
 ESP_LOGI(TAG, " Still waiting... (%d/%d seconds)", elapsed, timeout_seconds);
 }
 
 vTaskDelay(pdMS_TO_TICKS(1000));
 elapsed++;
 }
 
 ESP_LOGW(TAG, "Network registration timeout after %d seconds", timeout_seconds);
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
 
 ESP_LOGI(TAG, "Testing connectivity to %s...", host);
 
 const lte_interface_t* lte = lte_get_interface();
 if (!lte) {
 strcpy(result->error_message, "LTE interface not available");
 return false;
 }
 
 // Enable network PDP context first
 ESP_LOGI(TAG, "Activating PDP context...");
 at_response_t response = {0};
 
 // Initialize APN manager and ensure APN is configured
 ESP_LOGI(TAG, "Initializing APN management...");
 const apn_manager_interface_t* apn_mgr = apn_manager_get_interface();
 if (apn_mgr) {
 apn_config_t apn_config = APN_CONFIG_DEFAULT;
 apn_config.debug = true;
 
 if (apn_mgr->init(&apn_config)) {
 // Check if APN is already configured
 apn_status_t apn_status;
 if (apn_mgr->get_status(&apn_status) && apn_status.is_configured) {
 ESP_LOGI(TAG, "APN already configured: %s", apn_status.current_apn);
 } else {
 ESP_LOGI(TAG, "Setting default APN: m2mglobal");
 if (!apn_mgr->set_apn("m2mglobal", "", "")) {
 strcpy(result->error_message, "Failed to configure APN via APN manager");
 return false;
 }
 }
 } else {
 strcpy(result->error_message, "Failed to initialize APN manager");
 return false;
 }
 } else {
 // Fallback to direct APN setting if APN manager not available
 ESP_LOGW(TAG, "APN manager not available, using direct APN setting");
 char apn_cmd[128];
 snprintf(apn_cmd, sizeof(apn_cmd), "AT+CGDCONT=1,\"IP\",\"m2mglobal\"");
 
 if (!lte->send_at_command(apn_cmd, &response, 5000) || !response.success) {
 strcpy(result->error_message, "Failed to set APN directly");
 return false;
 }
 }
 
 // Activate PDP context
 if (!lte->send_at_command("AT+CGACT=1,1", &response, 10000) || !response.success) {
 strcpy(result->error_message, "Failed to activate PDP context");
 return false;
 }
 
 ESP_LOGI(TAG, "PDP context activated");
 
 // Ping command
 char ping_cmd[256];
 snprintf(ping_cmd, sizeof(ping_cmd), "AT+CPING=\"%s\",1,4,64,1000", host);
 
 ESP_LOGI(TAG, "Sending ping command...");
 uint32_t ping_start = xTaskGetTickCount();
 
 if (lte->send_at_command(ping_cmd, &response, 15000) && response.success) {
 uint32_t ping_end = xTaskGetTickCount();
 result->response_time_ms = (ping_end - ping_start) * portTICK_PERIOD_MS;
 
 // Check for successful ping response
 if (strstr(response.response, "+CPING:") && 
 (strstr(response.response, "OK") || strstr(response.response, "64"))) {
 result->ping_success = true;
 strcpy(result->error_message, "Success");
 ESP_LOGI(TAG, "Ping successful in %d ms", result->response_time_ms);
 return true;
 }
 }
 
 strcpy(result->error_message, "Ping timeout or failure");
 ESP_LOGW(TAG, "Ping failed: %s", response.response);
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
 ESP_LOGI(TAG, "Initializing GPS (SIM7670G GNSS)...");
 
 const lte_interface_t* lte = lte_get_interface();
 if (!lte) {
 ESP_LOGE(TAG, "LTE interface not available for GPS");
 return false;
 }
 
 at_response_t response = {0};
 
 // Step 1: Power on GNSS (as per Waveshare documentation)
 // NOTE: Removed AT+CGNSSPORTSWITCH - not documented in official Waveshare reference
 ESP_LOGI(TAG, "Powering on GNSS module (Waveshare official method)...");
 if (!lte->send_at_command("AT+CGNSSPWR=1", &response, 5000)) {
 ESP_LOGE(TAG, "Failed to power on GNSS");
 return false;
 }
 
 // Check for READY response
 if (response.success && strstr(response.response, "READY")) {
 ESP_LOGI(TAG, "GNSS powered on successfully");
 } else {
 ESP_LOGW(TAG, "GNSS power response: %s", response.response);
 }
 
 // Step 2: Enable GNSS engine (CRITICAL - missing command!)
 ESP_LOGI(TAG, "Enabling GNSS engine (AT+CGNSS=1)...");
 if (!lte->send_at_command("AT+CGNSS=1", &response, 5000)) {
 ESP_LOGE(TAG, "Failed to enable GNSS engine");
 return false;
 }
 
 if (response.success) {
 ESP_LOGI(TAG, "GNSS engine enabled successfully");
 } else {
 ESP_LOGW(TAG, "GNSS engine response: %s", response.response);
 }
 
 // Step 3: Wait for GNSS to initialize
 ESP_LOGI(TAG, "Waiting for GNSS initialization...");
 vTaskDelay(pdMS_TO_TICKS(2000));
 
 // Step 4: Enable GNSS data output (Waveshare official method)
 ESP_LOGI(TAG, "Enabling GNSS data output (AT+CGNSSTST=1)...");
 if (!lte->send_at_command("AT+CGNSSTST=1", &response, 3000)) {
 ESP_LOGE(TAG, "Failed to enable GNSS data output");
 return false;
 }
 
 if (response.success) {
 ESP_LOGI(TAG, "GNSS data output enabled");
 } else {
 ESP_LOGW(TAG, "GNSS data response: %s", response.response);
 }
 
 // Step 5: Switch GNSS to dedicated port (CRITICAL - from Waveshare example!)
 ESP_LOGI(TAG, "Switching GNSS to dedicated port (AT+CGNSSPORTSWITCH=0,1)...");
 if (!lte->send_at_command("AT+CGNSSPORTSWITCH=0,1", &response, 3000)) {
 ESP_LOGE(TAG, "Failed to switch GNSS port");
 return false;
 }
 
 if (response.success) {
 ESP_LOGI(TAG, "GNSS port switched - NMEA data now streaming to UART");
 } else {
 ESP_LOGW(TAG, "GNSS port switch response: %s", response.response);
 }
 
 // Step 6: Wait for NMEA stream to start (after port switch)
 ESP_LOGI(TAG, "Waiting for NMEA stream to stabilize after port switch...");
 vTaskDelay(pdMS_TO_TICKS(3000)); // Give GPS time to start streaming to new port
 
 // Step 7: Monitor for streaming NMEA data (should be continuous now)
 ESP_LOGI(TAG, "Monitoring for continuous NMEA stream...");
 if (lte->read_raw_data) {
 char nmea_stream_buffer[1024];
 memset(nmea_stream_buffer, 0, sizeof(nmea_stream_buffer));
 size_t bytes_read = 0;
 
 // Monitor for 5 seconds to detect streaming NMEA
 bool success = lte->read_raw_data(nmea_stream_buffer, sizeof(nmea_stream_buffer) - 1, &bytes_read, 5000);
 if (success && bytes_read > 0) {
 ESP_LOGI(TAG, " Detected %d bytes of streaming data from UART:", bytes_read);
 ESP_LOG_BUFFER_CHAR(TAG, nmea_stream_buffer, bytes_read);
 
 // Count NMEA sentences
 int nmea_count = 0;
 char* pos = nmea_stream_buffer;
 while ((pos = strstr(pos, "$G")) != NULL) {
 nmea_count++;
 pos++; // Move past this sentence
 }
 
 if (nmea_count > 0) {
 ESP_LOGI(TAG, "SUCCESS! Found %d NMEA sentences streaming from GPS!", nmea_count);
 ESP_LOGI(TAG, "GPS NMEA streaming is now active after AT+CGNSSPORTSWITCH");
 } else {
 ESP_LOGW(TAG, "Data found but no NMEA sentences detected");
 }
 } else {
 ESP_LOGW(TAG, "No streaming NMEA data detected after 5 seconds");
 }
 }
 
 // Step 8: Test GPS polling (revert to working AT+CGNSINF method)
 ESP_LOGI(TAG, "Testing GPS functionality (reverting to working method)...");
 
 at_response_t diag_response;
 
 // Test if NMEA data is flowing to UART buffer (THE CORRECT WAVESHARE METHOD!)
 ESP_LOGI(TAG, "Testing NMEA data availability from UART buffer...");
 
 // Wait a moment for NMEA data to start flowing
 vTaskDelay(pdMS_TO_TICKS(1000));
 
 // Test reading raw UART data to see if NMEA sentences are present
 size_t bytes_read = 0;
 char test_buffer[512];
 if (lte->read_raw_data && lte->read_raw_data(test_buffer, sizeof(test_buffer) - 1, &bytes_read, 2000)) {
 test_buffer[bytes_read] = '\0';
 if (bytes_read > 0) {
 ESP_LOGI(TAG, "UART buffer has %d bytes of data", bytes_read);
 if (strstr(test_buffer, "$G") || strstr(test_buffer, "NMEA") || strstr(test_buffer, "GGA") || strstr(test_buffer, "RMC")) {
 ESP_LOGI(TAG, "NMEA sentences detected in UART buffer!");
 } else {
 ESP_LOGI(TAG, "UART data present but no NMEA sentences yet");
 }
 } else {
 ESP_LOGI(TAG, "No data in UART buffer yet (GPS may need more time)");
 }
 }
 
 // Try to check if GNSS is actually outputting data by reading status
 ESP_LOGI(TAG, "Checking GNSS status and configuration...");
 if (lte->send_at_command("AT+CGNSSTST?", &diag_response, 3000)) {
 ESP_LOGI(TAG, "GNSS status: %s", diag_response.response);
 }
 
 // GPS initialization complete - using polling mode
 
 // Step 6: Wait for GPS to stabilize (shorter time since we're using polling mode)
 ESP_LOGI(TAG, " Waiting for GPS to stabilize for polling mode...");
 vTaskDelay(pdMS_TO_TICKS(2000)); // Give GPS time to initialize for polling
 

 
 ESP_LOGI(TAG, " GPS initialization complete");
 return true;
}

/**
 * @brief Start GPS polling
 */
static bool start_gps_polling_impl(void)
{
 ESP_LOGI(TAG, " Starting GPS polling...");
 
 const lte_interface_t* lte = lte_get_interface();
 if (!lte) {
 return false;
 }
 
 at_response_t response = {0};
 
 // GPS polling is handled by reading NMEA data directly from UART
 // After AT+CGNSSPWR=1 and AT+CGNSSTST=1, GPS outputs NMEA sentences continuously
 ESP_LOGI(TAG, "� GPS polling ready - NMEA data available via UART");
 return true;
}

/**
 * @brief Read NMEA sentences directly from UART (SIM7670G Waveshare method)
 * After AT+CGNSSPWR=1 and AT+CGNSSTST=1, GPS data outputs as NMEA sentences to UART
 */
static bool read_nmea_from_uart(char* buffer, size_t buffer_size, int timeout_ms)
{
 const lte_interface_t* lte = lte_get_interface();
 if (!lte || !lte->read_raw_data) {
 ESP_LOGE(TAG, " LTE interface or raw data function not available");
 return false;
 }
 
 // Use LTE interface's raw UART reading with mutex protection
 ESP_LOGD(TAG, " Reading NMEA data from UART (timeout: %d ms)", timeout_ms);
 
 size_t bytes_read = 0;
 bool found_nmea = false;
 
 // Read raw UART data with mutex protection via LTE interface
 if (lte->read_raw_data(buffer, buffer_size - 1, &bytes_read, timeout_ms)) {
 buffer[bytes_read] = '\0'; // Null terminate
 
 if (bytes_read > 0) {
 ESP_LOGD(TAG, "� Read %d bytes from UART", bytes_read);
 
 // Check if we have NMEA sentences (start with $ and contain GPS data)
 if (strstr(buffer, "$G") || strstr(buffer, "$GNRMC") || strstr(buffer, "$GNGGA")) {
 found_nmea = true;
 ESP_LOGD(TAG, " Found NMEA sentences in UART data");
 } else {
 ESP_LOGD(TAG, " UART data (may contain NMEA): %.*s", (int)bytes_read, buffer);
 }
 } else {
 ESP_LOGD(TAG, " No UART data available");
 }
 } else {
 ESP_LOGD(TAG, " Failed to read from UART");
 }
 
 return found_nmea;
}

/**
 * @brief Parse NMEA sentence for GPS fix information
 */
static bool parse_nmea_sentence(const char* nmea, gps_fix_info_t* fix_info)
{
 if (!nmea || !fix_info) return false;
 
 // Look for GNGGA sentence (Global Positioning System Fix Data)
 if (strncmp(nmea, "$GNGGA", 6) == 0 || strncmp(nmea, "$GPGGA", 6) == 0) {
 char* tokens[15];
 char* nmea_copy = strdup(nmea);
 char* token = strtok(nmea_copy, ",");
 int token_count = 0;
 
 while (token && token_count < 15) {
 tokens[token_count] = token;
 token = strtok(NULL, ",");
 token_count++;
 }
 
 // GGA format: $GNGGA,time,lat,N/S,lon,E/W,quality,sats,hdop,alt,M,geoid,M,dgps_time,dgps_id*checksum
 if (token_count >= 6 && strlen(tokens[2]) > 0 && strlen(tokens[4]) > 0) {
 int quality = atoi(tokens[6]);
 if (quality > 0) { // 0 = no fix, 1+ = fix available
 fix_info->has_fix = true;
 
 // Parse latitude (DDMM.MMMM format)
 double lat = atof(tokens[2]);
 fix_info->latitude = (int)(lat / 100) + fmod(lat, 100.0) / 60.0;
 if (tokens[3][0] == 'S') fix_info->latitude = -fix_info->latitude;
 
 // Parse longitude (DDDMM.MMMM format) 
 double lon = atof(tokens[4]);
 fix_info->longitude = (int)(lon / 100) + fmod(lon, 100.0) / 60.0;
 if (tokens[5][0] == 'W') fix_info->longitude = -fix_info->longitude;
 
 // Store time
 strncpy(fix_info->fix_time, tokens[1], sizeof(fix_info->fix_time) - 1);
 
 ESP_LOGI(TAG, " GPS FIX from NMEA! Lat: %.6f, Lon: %.6f (Quality: %d)", 
 fix_info->latitude, fix_info->longitude, quality);
 
 free(nmea_copy);
 return true;
 }
 }
 
 free(nmea_copy);
 }
 
 return false;
}

/**
 * @brief Get current GPS fix information by reading NMEA data from UART
 * Using Waveshare SIM7670G method: direct NMEA reading after GNSS enable
 */
static bool get_gps_fix_impl(gps_fix_info_t* fix_info)
{
 if (!fix_info) {
 return false;
 }
 
 // Initialize fix info
 memset(fix_info, 0, sizeof(gps_fix_info_t));
 fix_info->has_fix = false;
 
 ESP_LOGI(TAG, " Reading comprehensive GPS status (SIM7670G method)...");
 
 const lte_interface_t* lte = lte_get_interface();
 if (!lte) {
 ESP_LOGE(TAG, " LTE interface not available for GPS");
 return false;
 }
 
 // First check for any existing NMEA data in UART buffer
 ESP_LOGI(TAG, " Pre-check: Monitoring raw UART for NMEA data...");
 if (lte->read_raw_data) {
 char raw_buffer[1024];
 memset(raw_buffer, 0, sizeof(raw_buffer));
 size_t bytes_read = 0;
 bool success = lte->read_raw_data(raw_buffer, sizeof(raw_buffer) - 1, &bytes_read, 5000); // 5 second timeout
 if (success && bytes_read > 0) {
 ESP_LOGI(TAG, " Pre-check found %d bytes of raw UART data:", bytes_read);
 ESP_LOGI(TAG, " Raw UART content: '%.*s'", bytes_read, raw_buffer);
 
 // Detailed NMEA analysis
 if (strstr(raw_buffer, "$G")) {
 ESP_LOGI(TAG, " NMEA SENTENCES FOUND! GPS is outputting NMEA data!");
 
 // Count different sentence types
 int gprmc_count = 0, gpgga_count = 0, gpgsa_count = 0, gpgsv_count = 0;
 char* ptr = raw_buffer;
 while ((ptr = strstr(ptr, "$GPRMC")) != NULL) { gprmc_count++; ptr++; }
 ptr = raw_buffer;
 while ((ptr = strstr(ptr, "$GPGGA")) != NULL) { gpgga_count++; ptr++; }
 ptr = raw_buffer; 
 while ((ptr = strstr(ptr, "$GPGSA")) != NULL) { gpgsa_count++; ptr++; }
 ptr = raw_buffer;
 while ((ptr = strstr(ptr, "$GPGSV")) != NULL) { gpgsv_count++; ptr++; }
 
 ESP_LOGI(TAG, " NMEA Sentence counts: RMC:%d GGA:%d GSA:%d GSV:%d", 
 gprmc_count, gpgga_count, gpgsa_count, gpgsv_count);
 } else {
 ESP_LOGI(TAG, " No NMEA sentences in 5-second monitoring period");
 ESP_LOGI(TAG, " This suggests NMEA output might not be properly enabled");
 }
 } else {
 ESP_LOGI(TAG, " No raw UART data during 5-second monitoring");
 ESP_LOGI(TAG, " This suggests no NMEA output is being generated");
 }
 }
 
 at_response_t response = {0};
 
 // === COMPREHENSIVE GNSS STATUS CHECK ===
 
 // 1. Check GNSS power status
 ESP_LOGI(TAG, " Checking GNSS power status...");
 if (lte->send_at_command("AT+CGNSSPWR?", &response, 3000) && response.success) {
 ESP_LOGI(TAG, " Power Status: %s", response.response);
 }
 
 // 2. Check GNSS test mode status 
 ESP_LOGI(TAG, " Checking GNSS test mode...");
 if (lte->send_at_command("AT+CGNSSTST?", &response, 3000) && response.success) {
 ESP_LOGI(TAG, " Test Mode: %s", response.response);
 }
 
 // 3. Get satellite information and parse satellite count
 ESP_LOGI(TAG, "Getting satellite information...");
 if (lte->send_at_command("AT+CGNSSINFO", &response, 5000) && response.success) {
 ESP_LOGI(TAG, " Satellite Info: %s", response.response);
 
 // FIXED: Parse satellite count from CGNSSINFO (last field)
 // Format: +CGNSSINFO: mode,fix,sats_used,sats_view,unused,lat,ns,lon,ew,unused,time,alt,unused,unused,hdop,pdop,vdop,satellite_count
 char* cgnssinfo_start = strstr(response.response, "+CGNSSINFO:");
 if (cgnssinfo_start && fix_info) {
 // Count commas to find last field (satellite count)
 char* pos = cgnssinfo_start;
 int comma_count = 0;
 while (*pos && comma_count < 17) { // Find 17th comma (before last field)
 if (*pos == ',') comma_count++;
 pos++;
 }
 if (comma_count >= 17 && *pos) {
 int sat_count = atoi(pos);
 if (sat_count > 0 && sat_count < 100) { // Sanity check
 fix_info->satellites_used = sat_count;
 ESP_LOGI(TAG, " PARSED: Satellites in use: %d", sat_count);
 }
 }
 }
 } else {
 ESP_LOGW(TAG, " AT+CGNSSINFO not available, trying alternatives...");
 }
 
 // 4. Try GNSS status command
 ESP_LOGI(TAG, " Getting GNSS status...");
 if (lte->send_at_command("AT+CGNSS?", &response, 3000) && response.success) {
 ESP_LOGI(TAG, " GNSS Status: %s", response.response);
 }
 
 // 5. Main GPS position info - NON-CRITICAL (may fail if no GPS fix yet)
 ESP_LOGI(TAG, " Getting GPS position data (non-critical test)...");
 if (!lte->send_at_command("AT+CGPSINFO", &response, 5000) || !response.success) {
 ESP_LOGW(TAG, " AT+CGPSINFO failed - GPS likely searching for satellites (normal during startup)");
 // Don't return false - this is expected behavior when no GPS fix available
 ESP_LOGI(TAG, " GPS initialization will continue - position data available once satellites acquired");
 } else {
 ESP_LOGI(TAG, " GPS Position (AT+CGPSINFO): %s", response.response);
 }
 
 // Check for any raw NMEA data in UART buffer after GPS query
 ESP_LOGI(TAG, " Checking for raw NMEA data after GPS query...");
 if (lte->read_raw_data) {
 char nmea_buffer[512];
 memset(nmea_buffer, 0, sizeof(nmea_buffer));
 size_t bytes_read = 0;
 bool success = lte->read_raw_data(nmea_buffer, sizeof(nmea_buffer) - 1, &bytes_read, 3000);
 if (success && bytes_read > 0) {
 ESP_LOGI(TAG, " Found %d bytes of raw UART data after GPS query:", bytes_read);
 ESP_LOGI(TAG, " Raw UART content: '%.*s'", bytes_read, nmea_buffer);
 
 // Check for NMEA sentences
 if (strstr(nmea_buffer, "$G")) {
 ESP_LOGI(TAG, " NMEA SENTENCES DETECTED in raw buffer!");
 
 // Look for specific NMEA sentence types
 if (strstr(nmea_buffer, "$GPRMC")) ESP_LOGI(TAG, " Found $GPRMC (Recommended Minimum)");
 if (strstr(nmea_buffer, "$GPGGA")) ESP_LOGI(TAG, " Found $GPGGA (Global Positioning System Fix Data)");
 if (strstr(nmea_buffer, "$GPGSA")) ESP_LOGI(TAG, " Found $GPGSA (GPS DOP and active satellites)");
 if (strstr(nmea_buffer, "$GPGSV")) ESP_LOGI(TAG, " Found $GPGSV (GPS Satellites in view)");
 } else {
 ESP_LOGI(TAG, " No NMEA sentences found in raw buffer");
 ESP_LOGI(TAG, " Raw buffer contains: non-NMEA data or AT responses");
 }
 } else {
 ESP_LOGI(TAG, " No additional raw UART data after GPS query");
 }
 }
 
 // Parse AT+CGPSINFO response: +CGPSINFO: <lat>,<lon>,<alt>,<UTC time>,<TTFF>,<num>,<speed>,<course>
 if (strstr(response.response, "+CGPSINFO:")) {
 char* cgpsinfo_line = strstr(response.response, "+CGPSINFO:");
 if (cgpsinfo_line) {
 // Store raw GPS data
 strncpy(fix_info->nmea_data, response.response, sizeof(fix_info->nmea_data) - 1);
 
 // Parse CGPSINFO response - handle empty fields properly
 char* tokens[10];
 char* line_copy = strdup(cgpsinfo_line);
 char* data_start = line_copy + 11; // Skip "+CGPSINFO: "
 int token_count = 0;
 
 // Manual parsing to handle empty comma-separated fields
 char* current = data_start;
 char* next_comma = strchr(current, ',');
 
 while (token_count < 10) {
 if (next_comma) {
 *next_comma = '\0'; // Terminate current token
 tokens[token_count] = current;
 current = next_comma + 1;
 next_comma = strchr(current, ',');
 } else {
 // Last token (or only token)
 tokens[token_count] = current;
 token_count++;
 break;
 }
 token_count++;
 }
 
 ESP_LOGI(TAG, " GPS Tokens parsed: %d", token_count);
 
 // Always process GPS response - even if coordinates are empty
 // FIXED: AT+CGPSINFO actual format: lat,lat_dir,lon,lon_dir,date,time,altitude,speed,course
 // NOT: lat,lon,alt,UTC,TTFF,satellites_used,speed,course as previously assumed
 if (token_count >= 9) {
 // Check if we have valid coordinates (non-empty lat/lon)
 bool has_coordinates = (strlen(tokens[0]) > 0 && strlen(tokens[2]) > 0);
 
 if (has_coordinates) {
 // GPS has valid fix with coordinates
 fix_info->has_fix = true;
 // FIXED: Correct AT+CGPSINFO format: lat,lat_dir,lon,lon_dir,date,time,altitude,speed,course
 fix_info->latitude = atof(tokens[0]); // tokens[0] = latitude DDMM.MMMM
 fix_info->longitude = atof(tokens[2]); // tokens[2] = longitude DDMM.MMMM 
 fix_info->altitude = atof(tokens[6]); // tokens[6] = altitude
 
 // Store UTC time from tokens[5]
 if (strlen(tokens[5]) > 0) {
 strncpy(fix_info->fix_time, tokens[5], sizeof(fix_info->fix_time) - 1);
 }
 
 ESP_LOGI(TAG, " GPS FIX ACQUIRED!");
 ESP_LOGI(TAG, " Position: %.6f, %.6f (altitude: %.1fm)", 
 fix_info->latitude, fix_info->longitude, fix_info->altitude);
 } else {
 // GPS running but no satellite fix yet 
 fix_info->has_fix = false;
 fix_info->latitude = 0.0;
 fix_info->longitude = 0.0;
 fix_info->altitude = 0.0;
 
 ESP_LOGI(TAG, " GPS ACTIVE - Searching for satellites...");
 ESP_LOGI(TAG, " Position: No fix yet (0.000000, 0.000000)");
 }
 
 // FIXED: AT+CGPSINFO does NOT contain satellite count
 // Satellite count comes from AT+CGNSSINFO command (handled separately)
 // Don't parse satellites from wrong field - this was causing 153633 bug
 fix_info->satellites_used = 0; // Will be updated by CGNSSINFO parsing
 
 ESP_LOGI(TAG, " Satellites: %d", fix_info->satellites_used);
 ESP_LOGI(TAG, " UTC Time: %s", strlen(fix_info->fix_time) > 0 ? fix_info->fix_time : "No time");
 
 // Show detailed field breakdown
 ESP_LOGI(TAG, " � Field Details:");
 ESP_LOGI(TAG, " Field 0 (Latitude): [%s]", tokens[0]);
 ESP_LOGI(TAG, " Field 1 (Longitude): [%s]", tokens[1]);
 ESP_LOGI(TAG, " Field 2 (Altitude): [%s]", tokens[2]);
 ESP_LOGI(TAG, " Field 3 (UTC Time): [%s]", tokens[3]);
 ESP_LOGI(TAG, " Field 4 (TTFF): [%s]", tokens[4]);
 ESP_LOGI(TAG, " Field 5 (Satellites): [%s]", tokens[5]);
 if (token_count > 6) ESP_LOGI(TAG, " Field 6 (Speed): [%s]", tokens[6]);
 if (token_count > 7) ESP_LOGI(TAG, " Field 7 (Course): [%s]", tokens[7]);
 
 free(line_copy);
 return true; // Always return true - GPS is responding
 } else {
 ESP_LOGW(TAG, " Unexpected GPS response format - not enough tokens");
 ESP_LOGI(TAG, " Raw data: %s", response.response);
 }
 
 free(line_copy);
 }
 } else {
 ESP_LOGI(TAG, " No +CGPSINFO found in response: %s", response.response);
 }
 
 ESP_LOGI(TAG, " GPS initialization complete - GPS powered on and NMEA output enabled");
 ESP_LOGI(TAG, " GPS will provide position data once satellites are acquired");
 return true; // GPS initialization successful, position fix will come later
}
/* 
 // Remove carriage return if present (BROKEN CODE - COMMENTED OUT)
 if (line_end > line_start && *(line_end - 1) == '\r') {
 *(line_end - 1) = '\0';
 }
 
 ESP_LOGD(TAG, "� NMEA Line: %s", line_start);
 
 // Try to parse this NMEA sentence
 if (parse_nmea_sentence(line_start, fix_info)) {
 ESP_LOGI(TAG, " Successfully parsed GPS fix from NMEA data");
 return true;
 }
 
 line_start = line_end + 1;
 }
 
 // Process the last line if it doesn't end with newline
 if (strlen(line_start) > 0) {
 ESP_LOGD(TAG, "📝 NMEA Line (last): %s", line_start);
 if (parse_nmea_sentence(line_start, fix_info)) {
 ESP_LOGI(TAG, " Successfully parsed GPS fix from NMEA data");
 return true;
 }
 }
 
 ESP_LOGI(TAG, " NMEA data received but no valid GPS fix found yet");
 return true; // We got data, just no fix yet
 } else {
 ESP_LOGD(TAG, " No NMEA data available from UART");
 return false;
 }
*/ // END BROKEN CODE

/**
 * @brief Wait for GPS fix with timeout
 */
static bool wait_for_gps_fix_impl(int timeout_seconds, gps_fix_info_t* fix_info)
{
 ESP_LOGI(TAG, " Waiting for GPS fix (timeout: %d seconds)...", timeout_seconds);
 ESP_LOGI(TAG, " Ensure GPS antenna is connected and device is outdoors");
 
 int elapsed = 0;
 
 while (elapsed < timeout_seconds) {
 if (get_gps_fix_impl(fix_info) && fix_info->has_fix) {
 ESP_LOGI(TAG, " GPS fix acquired after %d seconds!", elapsed);
 ESP_LOGI(TAG, " Position: %.6f, %.6f (altitude: %.1fm)", 
 fix_info->latitude, fix_info->longitude, fix_info->altitude);
 ESP_LOGI(TAG, " Satellites: %d", fix_info->satellites_used);
 ESP_LOGI(TAG, " Fix time: %s", fix_info->fix_time);
 return true;
 }
 
 // Show progress every 10 seconds 
 if (elapsed % 10 == 0) {
 ESP_LOGI(TAG, " Still searching for satellites... (%d/%d seconds)", elapsed, timeout_seconds);
 if (fix_info && fix_info->satellites_used > 0) {
 ESP_LOGI(TAG, " Satellites visible: %d", fix_info->satellites_used);
 }
 }
 
 vTaskDelay(pdMS_TO_TICKS(5000)); // Slow down GPS polling to every 5 seconds (was 1 second)
 elapsed += 5;
 }
 
 ESP_LOGW(TAG, " GPS fix timeout after %d seconds", timeout_seconds);
 ESP_LOGI(TAG, " Try moving to a location with better sky visibility");
 return false;
}

/**
 * @brief Print modem status
 */
static void print_status_impl(void)
{
 ESP_LOGI(TAG, " === MODEM STATUS ===");
 
 modem_status_t status = get_modem_status_impl();
 
 switch (status) {
 case MODEM_STATUS_FAILED:
 ESP_LOGI(TAG, " Status: Failed/Not Ready");
 break;
 case MODEM_STATUS_READY:
 ESP_LOGI(TAG, " Status: Ready (SIM not ready)");
 break;
 case MODEM_STATUS_SIM_READY:
 ESP_LOGI(TAG, " Status: 🟠 SIM Ready (Network not registered)");
 break;
 case MODEM_STATUS_NETWORK_REGISTERED:
 ESP_LOGI(TAG, " Status: Network Registered");
 break;
 default:
 ESP_LOGI(TAG, " Status: ❓ Unknown");
 break;
 }
 
 // Test connectivity
 network_test_result_t ping_result = {0};
 if (ping_google_impl(&ping_result)) {
 ESP_LOGI(TAG, " Internet: Connected (ping: %dms)", ping_result.response_time_ms);
 } else {
 ESP_LOGI(TAG, " Internet: Not connected (%s)", ping_result.error_message);
 }
 
 // Test GPS
 gps_fix_info_t gps_info = {0};
 if (get_gps_fix_impl(&gps_info)) {
 if (gps_info.has_fix) {
 ESP_LOGI(TAG, " GPS: Fixed (%.6f, %.6f, %d sats)", 
 gps_info.latitude, gps_info.longitude, gps_info.satellites_used);
 } else {
 ESP_LOGI(TAG, " GPS: Active but no fix (%d sats visible)", gps_info.satellites_used);
 }
 } else {
 ESP_LOGI(TAG, " GPS: Not active");
 }
 
 ESP_LOGI(TAG, "======================");
}

/**
 * @brief Reset modem (software reset)
 */
static void reset_modem_impl(void)
{
 ESP_LOGI(TAG, " Resetting modem...");
 
 const lte_interface_t* lte = lte_get_interface();
 if (lte) {
 at_response_t response = {0};
 lte->send_at_command("AT+CFUN=1,1", &response, 10000);
 ESP_LOGI(TAG, " Reset command sent");
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
 ESP_LOGI(TAG, " Modem initialization module created");
 
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
 ESP_LOGI(TAG, " Modem initialization module destroyed");
 }
}

/**
 * @brief Complete modem initialization sequence
 * Implements Waveshare SIM7670G recommended startup procedure
 */
bool modem_init_complete_sequence(int timeout_seconds)
{
 ESP_LOGI(TAG, " === STARTING COMPLETE MODEM INITIALIZATION SEQUENCE ===");
 ESP_LOGI(TAG, "📖 Following Waveshare SIM7670G recommended procedure");
 
 modem_init_interface_t* modem = modem_init_create();
 if (!modem) {
 ESP_LOGE(TAG, " Failed to create modem interface");
 return false;
 }
 
 // Step 1: Test modem readiness
 ESP_LOGI(TAG, "\n STEP 1: Testing modem readiness");
 if (!modem->test_modem_ready()) {
 ESP_LOGE(TAG, " Modem readiness test failed");
 return false;
 }
 ESP_LOGI(TAG, " STEP 1 COMPLETE: Modem is ready");
 
 // Step 2: Wait for network registration
 ESP_LOGI(TAG, "\n STEP 2: Waiting for network registration");
 if (!modem->wait_for_network(timeout_seconds / 2)) {
 ESP_LOGW(TAG, " Network registration timeout, but continuing...");
 } else {
 ESP_LOGI(TAG, " STEP 2 COMPLETE: Network registered");
 }
 
 // Step 3: Test connectivity
 ESP_LOGI(TAG, "\n STEP 3: Testing internet connectivity");
 network_test_result_t ping_result = {0};
 if (modem->ping_google(&ping_result)) {
 ESP_LOGI(TAG, " STEP 3 COMPLETE: Internet connectivity confirmed (ping: %dms)", ping_result.response_time_ms);
 } else {
 ESP_LOGW(TAG, " Internet connectivity test failed: %s", ping_result.error_message);
 ESP_LOGI(TAG, " Continuing with GPS initialization anyway...");
 }
 
 // Step 4: Initialize GPS
 ESP_LOGI(TAG, "\n STEP 4: Initializing GPS");
 if (!modem->initialize_gps()) {
 ESP_LOGE(TAG, " GPS initialization failed");
 return false;
 }
 ESP_LOGI(TAG, " STEP 4 COMPLETE: GPS initialized");
 
 // Step 5: Wait for GPS fix
 ESP_LOGI(TAG, "\n STEP 5: Waiting for GPS fix");
 gps_fix_info_t gps_info = {0};
 if (modem->wait_for_gps_fix(timeout_seconds, &gps_info)) {
 ESP_LOGI(TAG, " STEP 5 COMPLETE: GPS fix acquired!");
 ESP_LOGI(TAG, " Location: %.6f, %.6f (altitude: %.1fm)", 
 gps_info.latitude, gps_info.longitude, gps_info.altitude);
 ESP_LOGI(TAG, " Satellites: %d", gps_info.satellites_used);
 } else {
 ESP_LOGW(TAG, " GPS fix timeout, but GPS is active and searching");
 }
 
 // Final status
 ESP_LOGI(TAG, "\n FINAL STATUS:");
 modem->print_status();
 
 ESP_LOGI(TAG, "\n === MODEM INITIALIZATION SEQUENCE COMPLETE ===");
 return true;
}
