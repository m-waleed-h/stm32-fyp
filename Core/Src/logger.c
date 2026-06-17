/**
 * logger.c
 * --------
 * UART-based logging + boot diagnostics for STM32 Traffic Controller.
 *
 * Uses same UART as Orange Pi communication (USART1, 115200 baud).
 * Log lines start with [LEVEL][TAG] so Orange Pi can filter them out
 * and only process {"type":...} JSON messages.
 *
 * Boot diagnostic runs BEFORE osKernelStart() — no FreeRTOS conflicts.
 * Runtime logging uses direct HAL_UART_Transmit — acceptable for debug use.
 */

#include "logger.h"
#include "rtc_manager.h"
#include "ds3231.h"
#include <stdio.h>
#include <string.h>

// ── Private ───────────────────────────────────────────────────
static UART_HandleTypeDef *_huart = NULL;
static char _logBuf[200];

// DS3231 I2C address (7-bit = 0x68, HAL needs 8-bit = 0xD0)
#define DS3231_I2C_ADDR   (0x68 << 1)

static const char *levelStr[] = {
    "[INFO ]",
    "[WARN ]",
    "[ERROR]",
};

// ── Init ─────────────────────────────────────────────────────
void Logger_Init(UART_HandleTypeDef *huart)
{
    _huart = huart;
}

// ── Core: send one log line ───────────────────────────────────
void Logger_Log(LogLevel_t level, const char *tag, const char *msg)
{
    if (_huart == NULL) return;

    int len = snprintf(_logBuf, sizeof(_logBuf),
                       "%s[%-8s]: %s\r\n",
                       levelStr[level], tag, msg);

    if (len > 0 && len < (int)sizeof(_logBuf)) {
        HAL_UART_Transmit(_huart, (uint8_t *)_logBuf, (uint16_t)len, 100);
    }
}

// ── Core: formatted log line ──────────────────────────────────
void Logger_LogF(LogLevel_t level, const char *tag, const char *fmt, ...)
{
    if (_huart == NULL) return;

    char msgBuf[150];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    Logger_Log(level, tag, msgBuf);
}

// ── Private: check if DS3231 responds on I2C ─────────────────
static uint8_t CheckRTC(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef result = HAL_I2C_IsDeviceReady(
        hi2c, DS3231_I2C_ADDR, 3, 100);
    return (result == HAL_OK) ? 1 : 0;
}

// ── Boot Diagnostic ───────────────────────────────────────────
void Logger_BootDiagnostic(I2C_HandleTypeDef *hi2c)
{
    if (_huart == NULL) return;

    // ── Header ────────────────────────────────────────────────
    Logger_Log(LOG_INFO, "BOOT",
               "========================================");
    Logger_Log(LOG_INFO, "BOOT",
               "  STM32 Smart Traffic Controller v1.0  ");
    Logger_Log(LOG_INFO, "BOOT",
               "  ITU FYP 2026 — Aina Ahmad BSEE22085  ");
    Logger_Log(LOG_INFO, "BOOT",
               "========================================");

    // ── System clock ──────────────────────────────────────────
    LOG_INFOF("BOOT", "System clock: %lu MHz",
              HAL_RCC_GetSysClockFreq() / 1000000);

    // ── RTC Check ─────────────────────────────────────────────
    LOG_INFO("RTC", "Checking DS3231 on I2C1 (PB8=SCL, PB9=SDA)...");

    if (CheckRTC(hi2c)) {
        LOG_INFO("RTC", "DS3231 connected OK");

        // Read and print current time
        RTC_TimeDate t;
        RTC_GetCurrentTime(&t);

        LOG_INFOF("RTC", "Current time: %02d:%02d:%02d  Date: %02d/%02d/20%02d",
                  t.hours, t.minutes, t.seconds,
                  t.date, t.month, t.year);

        // Warn if time looks unset (power loss reset RTC)
        if (t.hours == 0 && t.minutes == 0 && t.seconds == 0) {
            LOG_WARN("RTC",
                "Time is 00:00:00 — RTC may have lost power. "
                "Night mode will activate! Set time via DS3231_SetTime().");
        }

        // Warn if year looks wrong
        if (t.year < 25 || t.year > 30) {
            LOG_WARNF("RTC",
                "Year 20%02d looks incorrect — check RTC battery.", t.year);
        }

    } else {
        LOG_ERROR("RTC",
            "DS3231 NOT responding on I2C1 — check wiring (PB8/PB9) "
            "and pull-up resistors (4.7k to 3.3V).");
        LOG_ERROR("RTC",
            "RTC fallback DISABLED — system will use dummy timing only.");
    }

    // ── UART ─────────────────────────────────────────────────
    LOG_INFO("UART", "USART1 initialized — 115200 baud (PA9=TX, PB7=RX)");
    LOG_INFO("UART", "DMA2 Stream2 configured for RX (Circular mode)");
    LOG_INFO("UART", "Waiting for Orange Pi data: format A:XX,B:XX");

    // ── Traffic config ────────────────────────────────────────
    LOG_INFO("TRAFFIC", "Light panels: 2 (Panel1=Side A, Panel2=Side B)");
    LOG_INFOF("TRAFFIC", "Green time range: %d-%d seconds", 4, 14);
    LOG_INFOF("TRAFFIC", "Yellow time: 2 seconds (fixed)");
    LOG_INFOF("TRAFFIC",
        "Fallback dummy counts: A=%d vehicles, B=%d vehicles",
        5, 5);

    // ── FreeRTOS ─────────────────────────────────────────────
    LOG_INFO("RTOS", "Tasks: TrafficMgr(Normal) + UARTHandler(AboveNormal)");
    LOG_INFO("RTOS", "Tickless idle: ENABLED (low power between RTC ticks)");

    // ── Done ──────────────────────────────────────────────────
    LOG_INFO("BOOT", "Boot diagnostics complete — starting FreeRTOS...");
    Logger_Log(LOG_INFO, "BOOT",
               "========================================");
}
