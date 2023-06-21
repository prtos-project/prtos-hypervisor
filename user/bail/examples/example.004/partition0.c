#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define QPORT_NAME "portQ"
#define SPORT_NAME "portS"

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

char q_message[32];
char s_message[32];

void partition_main(void) {
    prtos_s32_t q_desc, s_desc, e;
    prtos_u32_t s_seq, q_seq;

    PRINT("Opening ports...\n");

    // Create ports
    // Parameters of creation calls must match XML configuration vector
    q_desc = prtos_create_queuing_port(QPORT_NAME, 16, 128, PRTOS_SOURCE_PORT);
    if (q_desc < 0) {
        PRINT("error %d\n", q_desc);
        goto end;
    }
    s_desc = prtos_create_sampling_port(SPORT_NAME, 128, PRTOS_SOURCE_PORT, 0);
    if (s_desc < 0) {
        PRINT("error %d\n", s_desc);
        goto end;
    }
    PRINT("done\n");

    PRINT("Generating messages...\n");
    s_seq = q_seq = 0;
    for (e = 0; e < 2; ++e) {
        sprintf(s_message, "<<sampling message %d>>", s_seq++);
        PRINT("SEND %s\n", s_message);
        prtos_write_sampling_message(s_desc, s_message, sizeof(s_message));
        prtos_idle_self();

        sprintf(q_message, "<<queuing message %d>>", q_seq++);
        PRINT("SEND %s\n", q_message);
        prtos_send_queuing_message(q_desc, q_message, sizeof(q_message));
        prtos_idle_self();
    }
    PRINT("Done\n");
    PRINT("Verification Passed\n");
    PRINT("Halting\n");
end:
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
