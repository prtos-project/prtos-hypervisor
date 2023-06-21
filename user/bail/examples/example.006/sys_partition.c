#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define PRINT(...)                                        \
    do {                                                  \
        prtos_time_t now;                                 \
        prtos_get_time(PRTOS_HW_CLOCK, &now);             \
        printf("[%d][%lld] ", PRTOS_PARTITION_SELF, now); \
        printf(__VA_ARGS__);                              \
    } while (0)

void partition_main(void) {
    prtos_s32_t seq = 0;
    prtos_u32_t prev, curr;

    curr = 0;
    while (seq < 6) {
        PRINT("Run %d\n", seq);
        if (seq % 2 == 0) {
            curr = (curr + 1) % 3;
            PRINT("Switch to plan %d --> ", curr % 3);
            if (prtos_switch_sched_plan(curr, &prev) < 0) {
                printf("FAILED\n");
            } else {
                printf("OK\n");
            }
        }
        seq = (seq + 1);
        prtos_idle_self();
    }
    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
