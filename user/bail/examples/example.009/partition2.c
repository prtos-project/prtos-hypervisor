#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define SHARED_ADDRESS 0x6300000

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

void ipi_ext_handler(trap_ctxt_t *ctxt) {
    prtos_u32_t value = *(volatile prtos_u32_t *)SHARED_ADDRESS;
    PRINT("READ SHM %d\n", value);
    if (value == 1) {
        PRINT("Verification Passed\n");
    }
}

void partition_main(void) {
    install_trap_handler(BAIL_PRTOSEXT_TRAP(PRTOS_VT_EXT_IPVI0), ipi_ext_handler);
    hw_sti();
    prtos_clear_irqmask(0, (1 << PRTOS_VT_EXT_IPVI0));  // Unmask port irqs

    PRINT("Waiting for messages\n");
    while (1)
        ;
}
