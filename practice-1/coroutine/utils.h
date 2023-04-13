#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static inline void fail(const char* message, const char* function, int line){
    printf("[x] Test failed at %s: %d: %s\n", function, line, message);
    exit(-1);
}

// undefined reference: 加上 static 即可