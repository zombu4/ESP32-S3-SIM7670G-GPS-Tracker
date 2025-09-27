#include "connection_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "../modem_init/modem_init.h"
#include "../gps/gps_module.h"
#include "../mqtt/mqtt_module.h"
#include "../lte/lte_module.h"

static const char *TAG = "CONN_MGR";

// Default Recovery Configuration
const recovery_config_t RECOVERY_CONFIG_DEFAULT = {
    .cellular_check_interval_ms = 30000,    // Check cellular every 30s
    .gps_check_interval_ms = 15000,         // Check GPS every 15s
    .mqtt_check_interval_ms = 10000,        // Check MQTT every 10s
    
    .cellular_timeout_ms = 120000,          // 2 minutes for cellular
    .gps_timeout_ms = 300000,               // 5 minutes for GPS fix
    .mqtt_timeout_ms = 30000,               // 30 seconds for MQTT
    
    .max_cellular_retries = 3,
    .max_gps_retries = 2,
    .max_mqtt_retries = 5,
    
    .auto_recovery_enabled = true,
    .debug_enabled = true
};

// Module state
static recovery_config_t current_config = {0};
static connection_status_t module_status = {0};
static bool module_initialized = false;
static TimerHandle_t monitoring_timer = NULL;
static bool monitoring_active = false;

// State tracking to prevent unnecessary restarts
static bool cellular_ever_initialized = false;
static bool gps_ever_initialized = false;
static bool mqtt_ever_connected = false;
static uint32_t last_cellular_init_time = 0;
static uint32_t last_gps_init_time = 0;
static uint32_t last_mqtt_connect_time = 0;

// Private function prototypes  
static bool conn_init_impl(const recovery_config_t* config);
static bool conn_deinit_impl(void);
static bool conn_startup_cellular_impl(uint32_t timeout_ms);
static bool conn_startup_gps_impl(uint32_t timeout_ms);
static bool conn_startup_mqtt_impl(uint32_t timeout_ms);
static bool conn_startup_full_system_impl(void);
static bool conn_check_all_connections_impl(connection_status_t* status);
static bool conn_is_cellular_healthy_impl(void);
static bool conn_is_gps_healthy_impl(void);
static bool conn_is_mqtt_healthy_impl(void);
static bool conn_is_system_ready_impl(void);
static bool conn_recover_cellular_impl(void);
static bool conn_recover_gps_impl(void);
static bool conn_recover_mqtt_impl(void);
static bool conn_recover_all_impl(void);
static bool conn_get_status_impl(connection_status_t* status);
static void conn_start_monitoring_impl(void);
static void conn_stop_monitoring_impl(void);
static void conn_set_debug_impl(bool enable);

// Helper functions
static void monitoring_timer_callback(TimerHandle_t xTimer);
static uint32_t get_timestamp_ms(void);
static bool wait_for_condition(bool (*check_func)(void), uint32_t timeout_ms, const char* description);

// Lightweight recovery functions (CPU efficient)
static bool conn_lightweight_cellular_check(void);
static bool conn_lightweight_gps_check(void);
static bool conn_lightweight_mqtt_check(void);
static bool conn_minimal_cellular_recovery(void);
static bool conn_minimal_mqtt_recovery(void);

// Connection Manager Interface
static const connection_manager_interface_t conn_interface = {
    .init = conn_init_impl,
    .deinit = conn_deinit_impl,
    .startup_cellular = conn_startup_cellular_impl,
    .startup_gps = conn_startup_gps_impl,
    .startup_mqtt = conn_startup_mqtt_impl,
    .startup_full_system = conn_startup_full_system_impl,
    .check_all_connections = conn_check_all_connections_impl,
    .is_cellular_healthy = conn_is_cellular_healthy_impl,
    .is_gps_healthy = conn_is_gps_healthy_impl,
    .is_mqtt_healthy = conn_is_mqtt_healthy_impl,
    .is_system_ready = conn_is_system_ready_impl,
    .recover_cellular = conn_recover_cellular_impl,
    .recover_gps = conn_recover_gps_impl,
    .recover_mqtt = conn_recover_mqtt_impl,
    .recover_all = conn_recover_all_impl,
    .get_status = conn_get_status_impl,
    .start_monitoring = conn_start_monitoring_impl,
    .stop_monitoring = conn_stop_monitoring_impl,
    .set_debug = conn_set_debug_impl
};

const connection_manager_interface_t* connection_manager_get_interface(void)
{
    return &conn_interface;
}

const char* connection_state_to_string(connection_state_t state)
{
    switch (state) {
        case CONN_STATE_DISCONNECTED: return "DISCONNECTED";
        case CONN_STATE_CONNECTING: return "CONNECTING";
        case CONN_STATE_CONNECTED: return "CONNECTED";
        case CONN_STATE_ERROR: return "ERROR";
        case CONN_STATE_RECOVERING: return "RECOVERING";
        default: return "UNKNOWN";
    }
}

static bool conn_init_impl(const recovery_config_t* config)
{
    if (module_initialized) {
        ESP_LOGW(TAG, "Connection manager already initialized");
        return true;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Recovery configuration is NULL");
        return false;
    }
    
    // Copy configuration
    memcpy(&current_config, config, sizeof(recovery_config_t));
    
    // Initialize status
    memset(&module_status, 0, sizeof(connection_status_t));
    module_status.cellular_state = CONN_STATE_DISCONNECTED;
    module_status.gps_state = CONN_STATE_DISCONNECTED;
    module_status.mqtt_state = CONN_STATE_DISCONNECTED;
    
    if (current_config.debug_enabled) {
        ESP_LOGI(TAG, "=== CONNECTION MANAGER INITIALIZATION ===");
        ESP_LOGI(TAG, "Cellular check interval: %d ms", current_config.cellular_check_interval_ms);
        ESP_LOGI(TAG, "GPS check interval: %d ms", current_config.gps_check_interval_ms);
        ESP_LOGI(TAG, "MQTT check interval: %d ms", current_config.mqtt_check_interval_ms);
        ESP_LOGI(TAG, "Auto recovery: %s", current_config.auto_recovery_enabled ? "Enabled" : "Disabled");
    }
    
    module_initialized = true;
    ESP_LOGI(TAG, "Connection manager initialized successfully");
    return true;
}

static bool conn_deinit_impl(void)
{
    if (!module_initialized) {
        return false;
    }
    
    // Stop monitoring if active
    conn_stop_monitoring_impl();
    
    module_initialized = false;
    ESP_LOGI(TAG, "Connection manager deinitialized");
    return true;
}

static bool conn_startup_cellular_impl(uint32_t timeout_ms)
{
    if (!module_initialized) {
        ESP_LOGE(TAG, "Connection manager not initialized");
        return false;
    }
    
    ESP_LOGI(TAG, "=== STARTING CELLULAR CONNECTION (Sequential Startup) ===");
    module_status.cellular_state = CONN_STATE_CONNECTING;
    
    uint32_t start_time = get_timestamp_ms();
    
    // Step 1: Initialize modem (includes APN setup)
    ESP_LOGI(TAG, "Step 1: Initializing modem and establishing cellular connection...");
    if (!modem_init_complete_sequence(120)) { // 2 minutes timeout
        ESP_LOGE(TAG, "Failed to complete modem initialization sequence");
        module_status.cellular_state = CONN_STATE_ERROR;
        return false;
    }
    
    // Step 2: Wait for network registration and PDP activation
    ESP_LOGI(TAG, "Step 2: Waiting for cellular connection to be fully established...");
    if (!wait_for_condition(conn_is_cellular_healthy_impl, timeout_ms, "cellular connection")) {
        ESP_LOGE(TAG, "Cellular connection failed or timed out");
        module_status.cellular_state = CONN_STATE_ERROR;
        return false;
    }
    
    module_status.cellular_state = CONN_STATE_CONNECTED;
    module_status.cellular_uptime = get_timestamp_ms() - start_time;
    
    // Mark cellular as successfully initialized (prevent unnecessary restarts)
    cellular_ever_initialized = true;
    last_cellular_init_time = get_timestamp_ms();
    
    ESP_LOGI(TAG, "✅ CELLULAR CONNECTION ESTABLISHED (took %d ms) - Marked as initialized", module_status.cellular_uptime);
    
    // Update detailed status
    conn_check_all_connections_impl(&module_status);
    
    if (current_config.debug_enabled && strlen(module_status.ip_address) > 0) {
        ESP_LOGI(TAG, "📶 Cellular Status: IP=%s, Signal=%d", 
                 module_status.ip_address, module_status.signal_strength);
    }
    
    return true;
}

static bool conn_startup_gps_impl(uint32_t timeout_ms)
{
    if (!module_initialized) {
        ESP_LOGE(TAG, "Connection manager not initialized");
        return false;
    }
    
    ESP_LOGI(TAG, "=== STARTING GPS SYSTEM (Sequential Startup) ===");
    module_status.gps_state = CONN_STATE_CONNECTING;
    
    uint32_t start_time = get_timestamp_ms();
    
    // Get GPS interface
    const gps_interface_t* gps = gps_get_interface();
    if (!gps) {
        ESP_LOGE(TAG, "GPS interface not available");
        module_status.gps_state = CONN_STATE_ERROR;
        return false;
    }
    
    // Step 1: Ensure GPS is initialized (should already be done by modem init)
    ESP_LOGI(TAG, "Step 1: Verifying GPS system status...");
    gps_status_t gps_status;
    if (!gps->get_status(&gps_status)) {
        ESP_LOGW(TAG, "Could not get GPS status - assuming GPS is ready");
    } else if (!gps_status.initialized) {
        ESP_LOGW(TAG, "GPS not initialized, but should have been done during modem startup");
    }
    
    // Step 2: Wait for GPS fix
    ESP_LOGI(TAG, "Step 2: Waiting for GPS satellite fix...");
    ESP_LOGI(TAG, "📡 Ensure GPS antenna is connected and device is outdoors");
    
    if (!wait_for_condition(conn_is_gps_healthy_impl, timeout_ms, "GPS satellite fix")) {
        ESP_LOGW(TAG, "GPS fix not acquired within timeout - continuing without fix");
        ESP_LOGW(TAG, "GPS will continue attempting to acquire fix in background");
        // Don't fail startup for GPS - it can continue trying in background
        module_status.gps_state = CONN_STATE_CONNECTING; // Keep trying
    } else {
        module_status.gps_state = CONN_STATE_CONNECTED;
        module_status.gps_uptime = get_timestamp_ms() - start_time;
        ESP_LOGI(TAG, "✅ GPS FIX ACQUIRED (took %d ms)", module_status.gps_uptime);
    }
    
    // Mark GPS as initialized (prevent unnecessary restarts)
    gps_ever_initialized = true;
    last_gps_init_time = get_timestamp_ms();
    ESP_LOGI(TAG, "📡 GPS SYSTEM MARKED AS INITIALIZED - Will only parse data from now on");
    
    // Update detailed status
    conn_check_all_connections_impl(&module_status);
    
    if (current_config.debug_enabled) {
        ESP_LOGI(TAG, "🛰️ GPS Status: Fix=%s, Satellites=%d/%d", 
                 module_status.gps_fix ? "YES" : "NO",
                 module_status.satellites_used, module_status.satellites_visible);
    }
    
    return true; // Always succeed - GPS can work in background
}

static bool conn_startup_mqtt_impl(uint32_t timeout_ms)
{
    if (!module_initialized) {
        ESP_LOGE(TAG, "Connection manager not initialized");
        return false;
    }
    
    // CRITICAL DEPENDENCY CHECK: Verify cellular is ready first
    if (!conn_is_cellular_healthy_impl()) {
        ESP_LOGE(TAG, "Cannot start MQTT - cellular connection not ready");
        return false;
    }
    
    // CRITICAL DEPENDENCY CHECK: Verify GPS has fix before MQTT initialization
    ESP_LOGI(TAG, "🔍 Checking GPS fix status before MQTT initialization...");
    if (!conn_is_gps_healthy_impl()) {
        ESP_LOGW(TAG, "⚠️ GPS fix not available - MQTT will wait for GPS fix");
        ESP_LOGI(TAG, "📡 MQTT initialization postponed until GPS acquires satellite fix");
        ESP_LOGI(TAG, "🕐 Please wait for GPS fix or move device outdoors for better satellite reception");
        
        // Wait for GPS fix with extended timeout
        ESP_LOGI(TAG, "⏳ Waiting for GPS fix before starting MQTT (timeout: %d ms)...", timeout_ms);
        if (!wait_for_condition(conn_is_gps_healthy_impl, timeout_ms, "GPS satellite fix")) {
            ESP_LOGW(TAG, "⏰ GPS fix timeout - starting MQTT anyway (GPS will continue in background)");
            ESP_LOGI(TAG, "📡 MQTT will start without GPS fix - GPS continues satellite acquisition");
        } else {
            ESP_LOGI(TAG, "✅ GPS fix acquired - proceeding with MQTT initialization");
        }
    } else {
        ESP_LOGI(TAG, "✅ GPS fix already available - proceeding with MQTT initialization");
    }
    
    ESP_LOGI(TAG, "=== STARTING MQTT CONNECTION (Sequential Startup) ===");
    ESP_LOGI(TAG, "📋 Prerequisites: Cellular ✅ | GPS %s", 
             conn_is_gps_healthy_impl() ? "✅" : "⏳ (continuing)");
    module_status.mqtt_state = CONN_STATE_CONNECTING;
    
    uint32_t start_time = get_timestamp_ms();
    
    // Get MQTT interface
    const mqtt_interface_t* mqtt = mqtt_get_interface();
    if (!mqtt) {
        ESP_LOGE(TAG, "MQTT interface not available");
        module_status.mqtt_state = CONN_STATE_ERROR;
        return false;
    }
    
    // Step 1: Initialize and connect MQTT (should NOT touch cellular/APN)
    ESP_LOGI(TAG, "Step 1: Connecting to MQTT broker (using existing cellular connection)...");
    if (!mqtt->connect()) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        module_status.mqtt_state = CONN_STATE_ERROR;
        return false;
    }
    
    // Step 2: Wait for MQTT connection to be established
    ESP_LOGI(TAG, "Step 2: Waiting for MQTT connection to be fully established...");
    if (!wait_for_condition(conn_is_mqtt_healthy_impl, timeout_ms, "MQTT connection")) {
        ESP_LOGE(TAG, "MQTT connection failed or timed out");
        module_status.mqtt_state = CONN_STATE_ERROR;
        return false;
    }
    
    module_status.mqtt_state = CONN_STATE_CONNECTED;
    module_status.mqtt_uptime = get_timestamp_ms() - start_time;
    
    // Mark MQTT as successfully connected (prevent unnecessary restarts)
    mqtt_ever_connected = true;
    last_mqtt_connect_time = get_timestamp_ms();
    
    ESP_LOGI(TAG, "✅ MQTT CONNECTION ESTABLISHED (took %d ms) - Marked as connected", module_status.mqtt_uptime);
    
    return true;
}

static bool conn_startup_full_system_impl(void)
{
    if (!module_initialized) {
        ESP_LOGE(TAG, "Connection manager not initialized");
        return false;
    }
    
    ESP_LOGI(TAG, "🚀 === STARTING FULL SYSTEM (Sequential Startup) ===");
    uint32_t total_start_time = get_timestamp_ms();
    
    // Step 1: CELLULAR (CRITICAL - Must succeed)
    ESP_LOGI(TAG, "🔧 Phase 1/3: Establishing Cellular Connection");
    if (!conn_startup_cellular_impl(current_config.cellular_timeout_ms)) {
        ESP_LOGE(TAG, "❌ SYSTEM STARTUP FAILED - Cellular connection failed");
        return false;
    }
    ESP_LOGI(TAG, "✅ Phase 1 Complete: Cellular Ready");
    
    // Step 2: GPS (NON-CRITICAL - Can continue in background)
    ESP_LOGI(TAG, "📡 Phase 2/3: Acquiring GPS Satellite Fix");
    if (!conn_startup_gps_impl(current_config.gps_timeout_ms)) {
        ESP_LOGW(TAG, "⚠️ Phase 2 Warning: GPS fix not acquired (will continue trying)");
    } else {
        ESP_LOGI(TAG, "✅ Phase 2 Complete: GPS Ready");
    }
    
    // Step 3: MQTT (CRITICAL - Needs cellular)
    ESP_LOGI(TAG, "📨 Phase 3/3: Establishing MQTT Connection");
    if (!conn_startup_mqtt_impl(current_config.mqtt_timeout_ms)) {
        ESP_LOGE(TAG, "❌ SYSTEM STARTUP FAILED - MQTT connection failed");
        return false;
    }
    ESP_LOGI(TAG, "✅ Phase 3 Complete: MQTT Ready");
    
    uint32_t total_time = get_timestamp_ms() - total_start_time;
    ESP_LOGI(TAG, "🎉 === FULL SYSTEM STARTUP COMPLETE (took %d ms) ===", total_time);
    
    // Start connection monitoring
    if (current_config.auto_recovery_enabled) {
        ESP_LOGI(TAG, "🔍 Starting automatic connection monitoring...");
        conn_start_monitoring_impl();
    }
    
    return true;
}

static bool conn_check_all_connections_impl(connection_status_t* status)
{
    if (!module_initialized) {
        return false;
    }
    
    // Update cellular status
    const lte_interface_t* lte = lte_get_interface();
    if (lte) {
        module_status.sim_ready = lte->check_sim_ready();
        module_status.network_registered = (lte->get_connection_status() == LTE_STATUS_CONNECTED);
        module_status.pdp_active = (lte->get_connection_status() == LTE_STATUS_CONNECTED);
        
        // Get signal strength  
        int rssi, quality;
        if (lte->get_signal_strength(&rssi, &quality)) {
            module_status.signal_strength = rssi;
        }
    }
    
    // Update GPS status
    const gps_interface_t* gps = gps_get_interface();
    if (gps) {
        gps_status_t gps_status;
        if (gps->get_status(&gps_status)) {
            module_status.gps_powered = gps_status.initialized && gps_status.gps_power_on;
        }
        
        gps_data_t gps_data;
        if (gps->read_data(&gps_data)) {
            module_status.gps_fix = gps_data.fix_valid;
            module_status.satellites_used = gps_data.satellites;
            module_status.satellites_visible = gps_data.satellites; // Use same value
        }
    }
    
    // Update MQTT status
    const mqtt_interface_t* mqtt = mqtt_get_interface();
    if (mqtt) {
        module_status.mqtt_connected = mqtt->is_connected();
    }
    
    module_status.last_check_time = get_timestamp_ms();
    
    if (status) {
        memcpy(status, &module_status, sizeof(connection_status_t));
    }
    
    return true;
}

static bool conn_is_cellular_healthy_impl(void)
{
    conn_check_all_connections_impl(NULL);
    return (module_status.sim_ready && 
            module_status.network_registered && 
            module_status.pdp_active);
}

static bool conn_is_gps_healthy_impl(void)
{
    conn_check_all_connections_impl(NULL);
    return (module_status.gps_powered && module_status.gps_fix);
}

static bool conn_is_mqtt_healthy_impl(void)
{
    conn_check_all_connections_impl(NULL);
    return module_status.mqtt_connected;
}

static bool conn_is_system_ready_impl(void)
{
    return (conn_is_cellular_healthy_impl() && 
            conn_is_mqtt_healthy_impl());
    // Note: GPS not required for system ready - can work without fix
}

static bool conn_recover_cellular_impl(void)
{
    // Only do full restart if cellular was never initialized OR it's been failing for a while
    if (!cellular_ever_initialized || 
        (get_timestamp_ms() - last_cellular_init_time) > 300000) { // 5 minutes since last init
        
        ESP_LOGI(TAG, "🔄 Full cellular recovery required (first time or long failure)...");
        module_status.cellular_state = CONN_STATE_RECOVERING;
        
        bool success = conn_startup_cellular_impl(current_config.cellular_timeout_ms);
        
        if (success) {
            ESP_LOGI(TAG, "✅ Cellular connection recovered via full restart");
        } else {
            ESP_LOGE(TAG, "❌ Cellular recovery failed");
            module_status.cellular_state = CONN_STATE_ERROR;
        }
        
        return success;
    } else {
        // Lightweight recovery - just try minimal cellular recovery
        ESP_LOGI(TAG, "🔧 Attempting lightweight cellular recovery...");
        return conn_minimal_cellular_recovery();
    }
}

static bool conn_recover_gps_impl(void)
{
    // GPS recovery is different - once initialized, we just continue reading data
    if (gps_ever_initialized) {
        ESP_LOGI(TAG, "� GPS already initialized - just continuing data reading (no restart needed)");
        module_status.gps_state = CONN_STATE_CONNECTING; // Keep trying to get fix
        return true;
    } else {
        // Only do full GPS startup if never initialized
        ESP_LOGI(TAG, "🔄 First-time GPS initialization...");
        module_status.gps_state = CONN_STATE_RECOVERING;
        
        bool success = conn_startup_gps_impl(current_config.gps_timeout_ms);
        
        if (success) {
            ESP_LOGI(TAG, "✅ GPS initialized successfully");
        } else {
            ESP_LOGI(TAG, "📡 GPS system started - will continue acquiring fix in background");
            module_status.gps_state = CONN_STATE_CONNECTING;
        }
        
        return true; // Always return true for GPS - it's non-critical
    }
}

static bool conn_recover_mqtt_impl(void)
{
    // Ensure cellular is healthy first
    if (!conn_is_cellular_healthy_impl()) {
        ESP_LOGW(TAG, "Cellular not healthy, recovering cellular first...");
        if (!conn_recover_cellular_impl()) {
            ESP_LOGE(TAG, "Cannot recover MQTT - cellular recovery failed");
            module_status.mqtt_state = CONN_STATE_ERROR;
            return false;
        }
    }
    
    // Try lightweight MQTT recovery first if we've connected before
    if (mqtt_ever_connected && 
        (get_timestamp_ms() - last_mqtt_connect_time) < 300000) { // Less than 5 minutes since last connection
        
        ESP_LOGI(TAG, "🔧 Attempting lightweight MQTT recovery...");
        return conn_minimal_mqtt_recovery();
    } else {
        // Full MQTT reconnection
        ESP_LOGI(TAG, "🔄 Full MQTT recovery required...");
        module_status.mqtt_state = CONN_STATE_RECOVERING;
        
        bool success = conn_startup_mqtt_impl(current_config.mqtt_timeout_ms);
        
        if (success) {
            ESP_LOGI(TAG, "✅ MQTT connection recovered");
        } else {
            ESP_LOGE(TAG, "❌ MQTT recovery failed");
            module_status.mqtt_state = CONN_STATE_ERROR;
        }
        
        return success;
    }
}

static bool conn_recover_all_impl(void)
{
    ESP_LOGI(TAG, "🔄 Recovering all connections...");
    
    bool success = true;
    
    // Recover in proper order: Cellular -> GPS -> MQTT
    if (!conn_is_cellular_healthy_impl()) {
        success &= conn_recover_cellular_impl();
    }
    
    if (!conn_is_gps_healthy_impl()) {
        conn_recover_gps_impl(); // Non-critical
    }
    
    if (!conn_is_mqtt_healthy_impl()) {
        success &= conn_recover_mqtt_impl();
    }
    
    return success;
}

static bool conn_get_status_impl(connection_status_t* status)
{
    if (!module_initialized || !status) {
        return false;
    }
    
    conn_check_all_connections_impl(status);
    return true;
}

static void conn_start_monitoring_impl(void)
{
    if (monitoring_active) {
        ESP_LOGW(TAG, "Connection monitoring already active");
        return;
    }
    
    // Create monitoring timer (use shortest interval for responsiveness)
    uint32_t interval = current_config.mqtt_check_interval_ms; // Most frequent
    
    monitoring_timer = xTimerCreate("conn_monitor", 
                                   pdMS_TO_TICKS(interval),
                                   pdTRUE, // Auto-reload
                                   NULL,   // Timer ID
                                   monitoring_timer_callback);
    
    if (monitoring_timer && xTimerStart(monitoring_timer, 0) == pdPASS) {
        monitoring_active = true;
        ESP_LOGI(TAG, "Connection monitoring started (interval: %d ms)", interval);
    } else {
        ESP_LOGE(TAG, "Failed to start connection monitoring timer");
    }
}

static void conn_stop_monitoring_impl(void)
{
    if (!monitoring_active) {
        return;
    }
    
    if (monitoring_timer) {
        xTimerStop(monitoring_timer, 0);
        xTimerDelete(monitoring_timer, 0);
        monitoring_timer = NULL;
    }
    
    monitoring_active = false;
    ESP_LOGI(TAG, "Connection monitoring stopped");
}

static void conn_set_debug_impl(bool enable)
{
    current_config.debug_enabled = enable;
    ESP_LOGI(TAG, "Debug mode %s", enable ? "enabled" : "disabled");
}

// Helper function implementations

static void monitoring_timer_callback(TimerHandle_t xTimer)
{
    if (!module_initialized || !current_config.auto_recovery_enabled) {
        return;
    }
    
    static uint32_t cellular_last_check = 0;
    static uint32_t gps_last_check = 0;
    static uint32_t mqtt_last_check = 0;
    
    uint32_t now = get_timestamp_ms();
    
    // Check cellular (least frequent) - Use lightweight check first
    if ((now - cellular_last_check) >= current_config.cellular_check_interval_ms) {
        if (!conn_lightweight_cellular_check()) {
            ESP_LOGW(TAG, "🔍 Monitoring: Cellular connection unhealthy - attempting smart recovery");
            conn_recover_cellular_impl(); // Uses intelligent recovery based on state
        } else if (current_config.debug_enabled) {
            ESP_LOGD(TAG, "📶 Cellular: Healthy (lightweight check)");
        }
        cellular_last_check = now;
    }
    
    // Check GPS - Just parse data, don't restart unless never initialized
    if ((now - gps_last_check) >= current_config.gps_check_interval_ms) {
        if (gps_ever_initialized) {
            // Just check if we can read data - don't restart GPS
            if (conn_lightweight_gps_check()) {
                if (current_config.debug_enabled) {
                    ESP_LOGD(TAG, "� GPS: Fix available (reading data only)");
                }
            } else if (current_config.debug_enabled) {
                ESP_LOGD(TAG, "📡 GPS: No fix yet (continuing to read data)");
            }
        } else {
            // GPS never initialized - try to initialize once
            ESP_LOGI(TAG, "📡 GPS not initialized - attempting first-time initialization");
            conn_recover_gps_impl();
        }
        gps_last_check = now;
    }
    
    // Check MQTT (most frequent) - Use lightweight check first
    if ((now - mqtt_last_check) >= current_config.mqtt_check_interval_ms) {
        if (!conn_lightweight_mqtt_check()) {
            ESP_LOGW(TAG, "🔍 Monitoring: MQTT connection unhealthy - attempting smart recovery");
            conn_recover_mqtt_impl(); // Uses intelligent recovery based on state
        } else if (current_config.debug_enabled) {
            ESP_LOGD(TAG, "📨 MQTT: Connected (lightweight check)");
        }
        mqtt_last_check = now;
    }
}

static uint32_t get_timestamp_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool wait_for_condition(bool (*check_func)(void), uint32_t timeout_ms, const char* description)
{
    if (!check_func || !description) {
        return false;
    }
    
    uint32_t start_time = get_timestamp_ms();
    uint32_t last_log_time = start_time;
    
    ESP_LOGI(TAG, "⏳ Waiting for %s (timeout: %d ms)...", description, timeout_ms);
    
    while ((get_timestamp_ms() - start_time) < timeout_ms) {
        if (check_func()) {
            uint32_t elapsed = get_timestamp_ms() - start_time;
            ESP_LOGI(TAG, "✅ %s ready (took %d ms)", description, elapsed);
            return true;
        }
        
        // Log progress every 10 seconds
        uint32_t now = get_timestamp_ms();
        if ((now - last_log_time) >= 10000) {
            uint32_t elapsed = now - start_time;
            ESP_LOGI(TAG, "⏳ Still waiting for %s (%d/%d ms)...", description, elapsed, timeout_ms);
            last_log_time = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every 1 second
    }
    
    ESP_LOGW(TAG, "⏰ Timeout waiting for %s", description);
    return false;
}

// =====================================================================
// LIGHTWEIGHT RECOVERY FUNCTIONS (CPU EFFICIENT)
// =====================================================================

static bool conn_lightweight_cellular_check(void)
{
    // Quick cellular health check without heavy operations
    const lte_interface_t* lte = lte_get_interface();
    if (!lte) return false;
    
    // Just check connection status - no AT commands or heavy operations
    lte_status_t status = lte->get_connection_status();
    return (status == LTE_STATUS_CONNECTED);
}

static bool conn_lightweight_gps_check(void)
{
    // Quick GPS check - just read current data without initialization
    const gps_interface_t* gps = gps_get_interface();
    if (!gps) return false;
    
    gps_data_t gps_data;
    if (gps->read_data(&gps_data)) {
        return gps_data.fix_valid;
    }
    return false;
}

static bool conn_lightweight_mqtt_check(void)
{
    // Quick MQTT health check
    const mqtt_interface_t* mqtt = mqtt_get_interface();
    if (!mqtt) return false;
    
    return mqtt->is_connected();
}

static bool conn_minimal_cellular_recovery(void)
{
    // Minimal cellular recovery - just try to reconnect without full restart
    const lte_interface_t* lte = lte_get_interface();
    if (!lte) return false;
    
    ESP_LOGI(TAG, "🔧 Minimal cellular recovery: checking SIM and reconnecting...");
    
    // Quick SIM check
    if (!lte->check_sim_ready()) {
        ESP_LOGW(TAG, "SIM not ready - may need full recovery");
        return false;
    }
    
    // Try to reconnect
    if (lte->connect()) {
        // Wait briefly for connection
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second wait
        
        if (lte->get_connection_status() == LTE_STATUS_CONNECTED) {
            ESP_LOGI(TAG, "✅ Minimal cellular recovery successful");
            module_status.cellular_state = CONN_STATE_CONNECTED;
            return true;
        }
    }
    
    ESP_LOGW(TAG, "⚠️ Minimal cellular recovery failed - may need full restart");
    return false;
}

static bool conn_minimal_mqtt_recovery(void)
{
    // Minimal MQTT recovery - just try to reconnect
    const mqtt_interface_t* mqtt = mqtt_get_interface();
    if (!mqtt) return false;
    
    ESP_LOGI(TAG, "🔧 Minimal MQTT recovery: reconnecting to broker...");
    
    module_status.mqtt_state = CONN_STATE_CONNECTING;
    
    // Try to reconnect
    if (mqtt->connect()) {
        // Wait briefly for connection
        vTaskDelay(pdMS_TO_TICKS(3000)); // 3 second wait
        
        if (mqtt->is_connected()) {
            ESP_LOGI(TAG, "✅ Minimal MQTT recovery successful");
            module_status.mqtt_state = CONN_STATE_CONNECTED;
            mqtt_ever_connected = true;
            last_mqtt_connect_time = get_timestamp_ms();
            return true;
        }
    }
    
    ESP_LOGW(TAG, "⚠️ Minimal MQTT recovery failed - may need full restart");
    module_status.mqtt_state = CONN_STATE_ERROR;
    return false;
}