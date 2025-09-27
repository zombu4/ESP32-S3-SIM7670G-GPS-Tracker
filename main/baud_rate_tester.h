// Baud Rate Testing for ESP32-S3-SIM7670G Hardware Debugging
// Created to systematically test UART communication

#ifndef BAUD_RATE_TESTER_H
#define BAUD_RATE_TESTER_H

#include "driver/uart.h"
#include "esp_log.h"

// Test baud rates in order of likelihood
static const int test_baud_rates[] = {
 115200, // Most common for SIM7670G
 9600, // Fallback standard
 57600, // Alternative high speed
 38400, // Alternative medium speed
 19200, // Alternative low speed
 460800, // Very high speed (sometimes used)
 230400, // High speed alternative
 14400, // Older standard
 4800, // Very low speed
 2400 // Emergency fallback
};

#define NUM_BAUD_RATES (sizeof(test_baud_rates) / sizeof(test_baud_rates[0]))

typedef struct {
 int working_baud_rate;
 bool found_working_rate;
 char response_buffer[256];
 int response_length;
} baud_test_result_t;

// Function to test UART communication at different baud rates
bool test_uart_baud_rates(int tx_pin, int rx_pin, baud_test_result_t* result);

// Function to configure UART with specific baud rate
bool configure_uart_with_baud(int tx_pin, int rx_pin, int baud_rate);

// Function to test AT command at current baud rate
bool test_at_command_simple(void);

#endif // BAUD_RATE_TESTER_H