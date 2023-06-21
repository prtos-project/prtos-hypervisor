#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

volatile prtos_s32_t seq;
#define SHARED_ADDRESS 0x6300000
#define DESTINATION_ID 2
// timer interrupt handler
void hw_timer_handler(trap_ctxt_t *ctxt) {
    PRINT("SHM WRITE %d\n", seq);
    *(volatile prtos_u32_t *)SHARED_ADDRESS = seq++;
    prtos_raise_partition_ipvi(DESTINATION_ID, PRTOS_VT_EXT_IPVI0);
}

void partition_main(void) {
    prtos_time_t hw_clock;
    // Install timer handler
    install_trap_handler(BAIL_PRTOSEXT_TRAP(PRTOS_VT_EXT_HW_TIMER), hw_timer_handler);

    hw_sti();                                              // Enable irqs
    prtos_clear_irqmask(0, (1 << PRTOS_VT_EXT_HW_TIMER));  // Unmask timer irqs

    prtos_get_time(PRTOS_HW_CLOCK, &hw_clock);  // Read hardware clock
    seq = 0;

    PRINT("Setting HW timer at 1 sec period\n");
    // Set hardware time driven timer
    prtos_set_timer(PRTOS_HW_CLOCK, hw_clock + 1000000LL, 1000000LL);

    while (seq < 2)
        ;

    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
