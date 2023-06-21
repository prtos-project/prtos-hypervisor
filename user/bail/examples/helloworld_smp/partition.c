#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

void partition_main(void) {
    printf("I'm Partition%d:vCPU%d,My name is %s\n", PRTOS_PARTITION_SELF, prtos_get_vcpuid(), prtos_get_pct()->name);
    printf("Verification Passed\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
