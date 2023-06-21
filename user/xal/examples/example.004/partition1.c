#include <string.h>  // string services provided by the libxal library
#include <stdio.h>   // stdio services provided by the libxal library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libxal library

#define QPORT_NAME "portQ"
#define SPORT_NAME "portS"

#define SHARED_ADDRESS 0x6300000

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

char sMessage[32];
char qMessage[32];
prtos_s32_t qDesc, sDesc, seq;

void QueuingExtHandler(trap_ctxt_t *ctxt) {
    if (prtos_receive_queuing_message(qDesc, qMessage, sizeof(qMessage)) > 0) {
        PRINT("RECEIVE %s\n", qMessage);
        PRINT("SHM WRITE %d\n", seq);
        *(volatile prtos_u32_t *)SHARED_ADDRESS = seq++;
    }
}

void sampling_ext_handler(trap_ctxt_t *ctxt) {
    prtos_u32_t flags;

    if (prtos_read_sampling_message(sDesc, sMessage, sizeof(sMessage), &flags) > 0) {
        PRINT("RECEIVE %s\n", sMessage);
    }
}

void partition_main(void) {
    PRINT("Opening ports...\n");
    qDesc = prtos_create_queuing_port(QPORT_NAME, 16, 128, PRTOS_DESTINATION_PORT);
    if (qDesc < 0) {
        PRINT("error %d\n", qDesc);
        goto end;
    }
    sDesc = prtos_create_sampling_port(SPORT_NAME, 128, PRTOS_DESTINATION_PORT, 0);
    if (sDesc < 0) {
        PRINT("error %d\n", sDesc);
        goto end;
    }
    PRINT("done\n");

    install_trap_handler(224 + PRTOS_VT_EXT_SAMPLING_PORT, sampling_ext_handler);
    install_trap_handler(224 + PRTOS_VT_EXT_QUEUING_PORT, QueuingExtHandler);
    hw_sti();

    // Unmask port irqs
    prtos_clear_irqmask(0, (1 << PRTOS_VT_EXT_SAMPLING_PORT) | (1 << PRTOS_VT_EXT_QUEUING_PORT));

    PRINT("Waiting for messages\n");
    while (1)
        ;

end:
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
