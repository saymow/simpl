#ifndef common_h
#define common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Enable NaN boxing
// more on NaN boxing: https://piotrduperas.com/posts/nan-boxing
#define NAN_BOXING

// ASCII Characters uppercase to lowercase offset 
#define ASCII_UPPERCASE_TO_LOWERCASE_OFFSET 32

// Debug program extensively
#ifdef DEBUG
    #define DEBUG_LOGS
    #define DEBUG_GC
#endif

// Debug program execution extensively
#ifdef DEBUG_LOGS
    // Log compiled bytecode
    #define DEBUG_PRINT_CODE
    
    // Log program execution, i.e, program stack and current instruction
    #define DEBUG_TRACE_EXECUTION
#endif

// Debug garbage collection extensively
#ifdef DEBUG_GC
    // Enable stressed gc mode.
    // All memory allocations trigger garbage collection.
    #define DEBUG_STRESS_GC
    
    // Log all memory allocation, deallocation
    // This is specially handful for triggering segmentation fault because of freed memory access.
    #define DEBUG_LOG_GC

    // Log all safezone transitions
    // This is specially handful for debugging deadlocks
    #define DEBUG_LOG_GC_SAFEZONE
#endif

// Total values handled by 1 byte or uint8_t 
#define UINT8_COUNT (UINT8_MAX + 1)

#endif