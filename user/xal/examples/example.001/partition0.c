#include <string.h>  // string services provided by the libxal library
#include <stdio.h>   // stdio services provided by the libxal library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libxal library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

volatile prtos_s32_t lock;

// Trap API
void hw_timer_handler(trap_ctxt_t *ctxt) {
    prtos_time_t hw, exec;

    prtos_get_time(PRTOS_HW_CLOCK, &hw);
    prtos_get_time(PRTOS_EXEC_CLOCK, &exec);
    PRINT("[%lld:%lld] IRQ HW Timer\n", hw, exec);
    ++lock;
}

void partition_main(void) {
    prtos_time_t hw_clock, exec_clock;
    // Install timer handler
    install_trap_handler(XAL_PRTOSEXT_TRAP(PRTOS_VT_EXT_HW_TIMER), hw_timer_handler);

    hw_sti();                                              // Enable irqs
    prtos_clear_irqmask(0, (1 << PRTOS_VT_EXT_HW_TIMER));  // Unmask timer irqs

    prtos_get_time(PRTOS_HW_CLOCK, &hw_clock);      // Read hardware clock
    prtos_get_time(PRTOS_EXEC_CLOCK, &exec_clock);  // Read execution clock

    lock = 0;

    PRINT("Setting HW timer at 1 sec period\n");
    // Set hardware time driven timer
    prtos_set_timer(PRTOS_HW_CLOCK, hw_clock + 1000000LL, 1000000LL);

    while (lock < 2)
        ;

    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
