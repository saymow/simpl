#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

void arityCheck(int expected, int received) {
    if (expected == received) return;
    
    printf("Expected %d arguments but received %d.", expected, received);
    exit(70);
}

