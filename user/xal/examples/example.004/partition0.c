#include <string.h>  // string services provided by the libxal library
#include <stdio.h>   // stdio services provided by the libxal library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libxal library

#define QPORT_NAME "portQ"
#define SPORT_NAME "portS"

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

char qMessage[32];
char sMessage[32];

void partition_main(void) {
    prtos_s32_t qDesc, sDesc, e;
    prtos_u32_t sSeq, qSeq;

    PRINT("Opening ports...\n");

    // Create ports
    // Parameters of creation calls must match XML configuration vector
    qDesc = prtos_create_queuing_port(QPORT_NAME, 16, 128, PRTOS_SOURCE_PORT);
    if (qDesc < 0) {
        PRINT("error %d\n", qDesc);
        goto end;
    }
    sDesc = prtos_create_sampling_port(SPORT_NAME, 128, PRTOS_SOURCE_PORT, 0);
    if (sDesc < 0) {
        PRINT("error %d\n", sDesc);
        goto end;
    }
    PRINT("done\n");

    PRINT("Generating messages...\n");
    sSeq = qSeq = 0;
    for (e = 0; e < 2; ++e) {
        sprintf(sMessage, "<<sampling message %d>>", sSeq++);
        PRINT("SEND %s\n", sMessage);
        prtos_write_sampling_message(sDesc, sMessage, sizeof(sMessage));
        prtos_idle_self();

        sprintf(qMessage, "<<queuing message %d>>", qSeq++);
        PRINT("SEND %s\n", qMessage);
        prtos_send_queuing_message(qDesc, qMessage, sizeof(qMessage));
        prtos_idle_self();
    }
    PRINT("Done\n");
    PRINT("Verification Passed\n");
    PRINT("Halting\n");
end:
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
