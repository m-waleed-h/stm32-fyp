#ifndef LOGGER_H
#define LOGGER_H

/**
 * logger.h
 * --------
 * Simple UART-based logging system for STM32 Traffic Controller.
 *
 * Output format:
 *   [INFO ][TAG     ]: message
 *   [WARN ][TAG     ]: message
 *   [ERROR][TAG     ]: message
 *
 * Boot sequence output example:
 *   [INFO ][BOOT    ]: ========================================
 *   [INFO ][BOOT    ]: STM32 Traffic Controller v1.0 starting
 *   [INFO ][RTC     ]: DS3231 connected OK — Time: 14:32:05
 *   [ERROR][RTC     ]: DS3231 not responding — Check I2C (PB8/PB9)
 *   [WARN ][CV      ]: No Orange Pi data — using dummy counts (5,5)
 */

#include "main.h"
#include <stdarg.h>

// ── Log levels ────────────────────────────────────────────────
typedef enum {
    LOG_INFO  = 0,
    LOG_WARN  = 1,
    LOG_ERROR = 2,
} LogLevel_t;

// ── Init ─────────────────────────────────────────────────────
void Logger_Init(UART_HandleTypeDef *huart);

// ── Core log functions ────────────────────────────────────────
void Logger_Log(LogLevel_t level, const char *tag, const char *msg);
void Logger_LogF(LogLevel_t level, const char *tag, const char *fmt, ...);

// ── Convenience macros ────────────────────────────────────────
#define LOG_INFO(tag, msg)       Logger_Log(LOG_INFO,  tag, msg)
#define LOG_WARN(tag, msg)       Logger_Log(LOG_WARN,  tag, msg)
#define LOG_ERROR(tag, msg)      Logger_Log(LOG_ERROR, tag, msg)

#define LOG_INFOF(tag, fmt, ...) Logger_LogF(LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOG_WARNF(tag, fmt, ...) Logger_LogF(LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOG_ERRORF(tag, fmt, ...) Logger_LogF(LOG_ERROR, tag, fmt, ##__VA_ARGS__)

// ── Boot diagnostic ───────────────────────────────────────────
// Call once before osKernelStart() — checks RTC and prints full boot report
void Logger_BootDiagnostic(I2C_HandleTypeDef *hi2c);

#endif /* LOGGER_H */
