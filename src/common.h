#ifndef common_h
#define common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IS_WINDOWS defined(_WIN32) || defined(_WIN64)

#define NAN_BOXING

#ifdef DEBUG
    #define DEBUG_LOGS
    #define DEBUG_GC
#endif

#ifdef DEBUG_LOGS
    #define DEBUG_PRINT_CODE
    #define DEBUG_TRACE_EXECUTION
#endif

#ifdef DEBUG_GC
    #define DEBUG_STRESS_GC
    #define DEBUG_LOG_GC
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

#endif