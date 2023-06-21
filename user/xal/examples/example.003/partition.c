#include <string.h>  // string services provided by the libxal library
#include <stdio.h>   // stdio services provided by the libxal library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libxal library

#define TRACE_MASK 0x3

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

void partition_main(void) {
    prtos_trace_event_t event;
    prtos_s32_t value;

    event.op_code_hi |= (PRTOS_TRACE_WARNING << TRACE_OPCODE_CRIT_BIT) & TRACE_OPCODE_CRIT_MASK;
    event.payload[0] = PRTOS_PARTITION_SELF;
    if (PRTOS_PARTITION_SELF == 0) {
        event.payload[1] = 0x0;
    } else {
        event.payload[1] = 0xFFFF;
    }

    prtos_s32_t times = 0;
    while (times < 2) {
        PRINT("New trace: %x\n", event.payload[1]);
        prtos_trace_event(0x01, &event);
        if (PRTOS_PARTITION_SELF == 0) {
            event.payload[1]++;
        } else {
            event.payload[1]--;
        }
        times++;
        prtos_idle_self();
    }
    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
