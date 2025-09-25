# SIM7670G AT Command Reference - VERIFIED WORKING COMMANDS
**Hardware**: ESP32-S3-SIM7670G (Waveshare)  
**Firmware**: ESP-IDF v5.5  
**Test Network**: T-Mobile (310260)  
**Test Date**: September 25, 2025

## ✅ CELLULAR/LTE COMMANDS - PRODUCTION VERIFIED

### **Basic Communication**
```bash
AT                          # Basic AT test
Response: OK                # May fail first attempt, retry once

AT+CFUN=1                   # Set full functionality  
Response: OK                # Takes ~150ms

AT+CPIN?                    # Check SIM card status
Response: +CPIN: READY      # SIM ready for use
          OK
```

### **Network Configuration**  
```bash
AT+CGDCONT=1,"IP","m2mglobal"   # Set APN context
Response: OK                     # APN successfully configured

AT+CREG?                    # Check network registration
Response: +CREG: 0,5        # 0=disabled notification, 5=roaming
          OK                # Successfully registered

AT+CGACT=1,1               # Activate PDP context  
Response: OK               # Data connection activated
```

### **Network Status Queries**
```bash
AT+COPS?                   # Query current operator
Response: +COPS: 0,2,"310260",7   # T-Mobile network
          OK

AT+CSQ                     # Signal quality
Response: +CSQ: 21,0       # Signal: 21/31 (good), BER: 0  
          OK
```

## 🔄 MQTT COMMANDS - PARTIALLY WORKING

### **MQTT Service Management**
```bash
AT+CMQTTDISC?              # Query MQTT connections
Response: +CMQTTDISC: 0,1   # Client 0: connected  
          +CMQTTDISC: 1,1   # Client 1: connected
          OK

AT+CMQTTSTOP               # Stop MQTT service
Response: +CMQTTSTOP: 19    # Stopped with code 19
          ERROR             # Normal response (not actual error)

AT+CMQTTSTART              # Start MQTT service  
Response: OK                # Service started
          +CMQTTSTART: 0    # Service ID 0 created
```

### **MQTT Client Management - ISSUE HERE**
```bash
# FAILING FORMATS:
AT+CMQTTACCQ=0,"esp32_gps_tracker_dev",0    # ❌ ERROR  
AT+CMQTTACCQ=0,"esp32_gps_tracker_dev",1    # ❌ ERROR (testing)
AT+CMQTTACCQ="esp32_gps_tracker_dev"        # ❌ ERROR (testing)

# POSSIBLE CORRECT FORMATS (to test):
AT+CMQTTACCQ=0,"esp32_gps_tracker_dev"      # Remove clean session flag
AT+CMQTTACCQ=0,esp32_gps_tracker_dev        # Remove quotes  
AT+CMQTTACCQ=0,"esp32_gps_tracker_dev",60   # Add keepalive
```

## 🔍 GPS/GNSS COMMANDS - FROM ARDUINO REFERENCE

### **GPS Power and Control**
```bash
AT+CGNSSPWR=1              # Power on GNSS module
AT+CGNSSTST=1              # Start GNSS test  
AT+CGNSSPORTSWITCH=0,1     # Switch GNSS to UART port
```

## 📊 TIMING AND RESPONSE PATTERNS

### **Command Response Times (Measured)**
```
AT              : 137ms (may timeout first time)
AT+CFUN=1       : 152ms  
AT+CPIN?        : 140ms
AT+CGDCONT      : 191ms
AT+CREG?        : 152ms  
AT+CGACT=1,1    : 154ms
AT+COPS?        : 152ms
AT+CSQ          : 150ms
AT+CMQTTSTART   : 131ms
```

### **Response Format Patterns**
```
Success Pattern: COMMAND\r\r\nOK\r\n
Error Pattern:   COMMAND\r\r\nERROR\r\n  
Data Pattern:    COMMAND\r\r\n+DATA: value\r\n\r\nOK\r\n
```

## ⚙️ UART CONFIGURATION - VERIFIED WORKING

### **Hardware Setup**
```c
#define UART_NUM           UART_NUM_1
#define UART_TX_PIN        18          // ESP32 -> SIM7670G RX
#define UART_RX_PIN        17          // ESP32 <- SIM7670G TX  
#define UART_BAUD_RATE     115200
#define UART_DATA_BITS     UART_DATA_8_BITS
#define UART_PARITY        UART_PARITY_DISABLE  
#define UART_STOP_BITS     UART_STOP_BITS_1
#define UART_FLOW_CTRL     UART_HW_FLOWCTRL_DISABLE
```

### **Command Format**  
```c
// All commands must end with \r\n
char full_command[256];
snprintf(full_command, sizeof(full_command), "%s\r\n", command);

// Send via UART
uart_write_bytes(UART_NUM_1, full_command, strlen(full_command));
```

## 🚨 CRITICAL DEBUGGING INSIGHTS

### **First AT Command Behavior**
- First `AT` command often returns `ERROR` - this is normal
- Always retry once before considering failure  
- Module needs warm-up time after power-on

### **Response Buffer Management**
- Responses can be split across multiple UART reads
- Must accumulate buffer until timeout or expected response found
- Look for `\r\n` terminators to detect complete responses

### **Timing Considerations**  
- Most commands respond within 150-200ms
- `AT+CGACT=1,1` can take up to 30 seconds (PDP activation)
- Always implement timeouts with retry logic

### **Error Response Handling**
- `ERROR` in MQTT context may not always indicate failure
- `+CMQTTSTOP: 19 ERROR` successfully stops service  
- Check for both `OK` and expected data patterns

---
**📋 NEXT TESTING PRIORITY**: Find correct AT+CMQTTACCQ format  
**🎯 SUCCESS CRITERIA**: MQTT client acquisition returns OK  
**⚡ FALLBACK PLAN**: HTTP POST implementation if MQTT AT commands fail