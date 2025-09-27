#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stream types from SIM7670G modem
typedef enum {
    STREAM_TYPE_GPS_NMEA,      // $GP*, $GL*, $GA*, $GB* sentences
    STREAM_TYPE_LTE_RESPONSE,  // +CREG, +CSQ, +COPS responses  
    STREAM_TYPE_MQTT_RESPONSE, // +CMQTT* responses
    STREAM_TYPE_AT_STATUS,     // OK, ERROR, READY status
    STREAM_TYPE_UNKNOWN        // Unclassified data
} modem_stream_type_t;

// Stream data packet
typedef struct {
    modem_stream_type_t type;
    char data[512];                // Raw data content
    size_t length;                 // Data length
    uint32_t timestamp_ms;         // Reception timestamp
    uint8_t priority;              // Processing priority (0=highest)
} modem_stream_packet_t;

// Stream router statistics
typedef struct {
    uint32_t total_packets_routed;
    uint32_t gps_packets;
    uint32_t lte_packets; 
    uint32_t mqtt_packets;
    uint32_t status_packets;
    uint32_t unknown_packets;
    uint32_t routing_errors;
} stream_router_stats_t;

// Stream processing callback
typedef void (*stream_processor_callback_t)(const modem_stream_packet_t* packet);

// Stream router interface
typedef struct {
    bool (*init)(void);
    bool (*start)(void);
    void (*stop)(void);
    bool (*register_processor)(modem_stream_type_t type, stream_processor_callback_t callback);
    void (*get_stats)(stream_router_stats_t* stats);
    void (*set_debug)(bool enable);
} stream_router_interface_t;

// Get stream router interface
const stream_router_interface_t* stream_router_get_interface(void);

#ifdef __cplusplus
}
#endif