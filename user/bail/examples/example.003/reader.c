/*
 * FILE: reader.c
 */
#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

static inline void read_trace(prtos_s32_t pid) {
    prtos_s32_t tid;
    prtos_trace_event_t event;

    tid = prtos_trace_open(pid);
    if (prtos_trace_read(tid, &event) > 0) {
        PRINT("[Trace] part_id: %x time: %lld opCode: %x payload: {%x,%x}\n", pid, event.timestamp, event.op_code_hi, event.payload[0], event.payload[1]);
    }
}

void partition_main(void) {
    PRINT(" --------- Trace Log ---------------\n");
    prtos_s32_t times = 0;
    while (times < 2) {
        read_trace(0);
        read_trace(1);
        times++;
        prtos_idle_self();
    }
    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
