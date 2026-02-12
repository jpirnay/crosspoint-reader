#pragma once

#include <HardwareSerial.h>

// Define ENABLE_SERIAL_LOG to enable logging
// Can be set in platformio.ini build_flags or as a compile definition

// Define LOG_LEVEL to control log verbosity:
// 0 = ERR only
// 1 = ERR + INF
// 2 = ERR + INF + DBG
// If not defined, defaults to 0

#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

#ifdef ENABLE_SERIAL_LOG
#if LOG_LEVEL >= 0
#define LOG_ERR(origin, format, ...) Serial.printf("[%lu] [ERR] [%s] " format "\n", millis(), origin, ##__VA_ARGS__)
#else
#define LOG_ERR(origin, format, ...)
#endif

#if LOG_LEVEL >= 1
#define LOG_INF(origin, format, ...) Serial.printf("[%lu] [INF] [%s] " format "\n", millis(), origin, ##__VA_ARGS__)
#else
#define LOG_INF(origin, format, ...)
#endif

#if LOG_LEVEL >= 2
#define LOG_DBG(origin, format, ...) Serial.printf("[%lu] [DBG] [%s] " format "\n", millis(), origin, ##__VA_ARGS__)
#else
#define LOG_DBG(origin, format, ...)
#endif
#else
#define LOG_DBG(origin, format, ...)
#define LOG_ERR(origin, format, ...)
#define LOG_INF(origin, format, ...)
#endif
