#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define SPORT_NAME "portS"

#define SHARED_ADDRESS 0x6300000

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

char s_message[32];
prtos_s32_t s_desc;
prtos_u32_t flags;

void sampling_ext_handler(trap_ctxt_t *ctxt) {
    prtos_u32_t flags;

    if (prtos_read_sampling_message(s_desc, s_message, sizeof(s_message), &flags) > 0) {
        PRINT("RECEIVE %s\n", s_message);
        PRINT("READ SHM %d\n", *(volatile prtos_u32_t *)SHARED_ADDRESS);
    }
}

void partition_main(void) {
    PRINT("Opening ports...\n");
    // Create ports
    s_desc = prtos_create_sampling_port(SPORT_NAME, 128, PRTOS_DESTINATION_PORT, 0);
    if (s_desc < 0) {
        PRINT("error %d\n", s_desc);
        goto end;
    }
    PRINT("done\n");

    install_trap_handler(BAIL_PRTOSEXT_TRAP(PRTOS_VT_EXT_SAMPLING_PORT), sampling_ext_handler);
    hw_sti();
    prtos_clear_irqmask(0, (1 << PRTOS_VT_EXT_SAMPLING_PORT));  // Unmask port irqs

    PRINT("Waiting for messages\n");
    while (1)
        ;

end:
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
