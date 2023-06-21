#include <prtos.h>  // hypercall services provided by the libprtos library
#include <stdio.h>  // stdio services provided by the libxal library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

void partition_main(void) {
    unsigned long counter = 0;

    PRINT("Partition %d\n", PRTOS_PARTITION_SELF);
    while (counter < 5) {
        counter++;
        PRINT("cnt: %d\n", counter);
        prtos_idle_self();
    }
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
