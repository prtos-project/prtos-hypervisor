#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define INVALID_ADDRESS 0x6100000

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

void partition_main(void) {
    prtos_u32_t value = 0xFFFFFFFF;
    *(volatile prtos_u32_t *)INVALID_ADDRESS = value;

    while (1) {
        ;
    }
}
