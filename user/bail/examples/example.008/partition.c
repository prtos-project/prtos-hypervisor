#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

void partition_main(void) {
    setup_vcpus();
    if (prtos_get_vcpuid() == 0) {
        PRINT("I'm Partition%d:vCPU%d,My name is %s\n\n", PRTOS_PARTITION_SELF, prtos_get_vcpuid(), prtos_get_pct()->name);
    } else {
        PRINT("I'm Partition%d:vCPU%d,My name is %s\n\n", PRTOS_PARTITION_SELF, prtos_get_vcpuid(), prtos_get_pct()->name);
    }

    if (prtos_get_vcpuid() == prtos_get_number_vcpus() - 1) {
        PRINT("Verification Passed\n");
    }
    while (1)
        ;
}
