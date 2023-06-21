#include <stdio.h>  // stdio services provided by the libbail library
#include <prtos.h>  // hypercall services provided by the libprtos library

#define P2 1
#define P3 2
#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

static inline int GetStatus(prtos_part_status_t *partStatus) {
    return (partStatus->state);
}

void PrintStatus(void) {
    static prtos_part_status_t partStatus;
    int st2, st3;

    prtos_get_partition_status(P2, &partStatus);
    st2 = GetStatus(&partStatus);  // partStatus->state;
    prtos_get_partition_status(P3, &partStatus);
    st3 = GetStatus(&partStatus);  //= partStatus->state;
    PRINT("Status P2 => 0x%x ;  P3 => 0x%x\n", st2, st3);
}

void partition_main(void) {
    int retValue;

    PRINT("Partition %d\n", PRTOS_PARTITION_SELF);

    PrintStatus();
    prtos_idle_self();
    retValue = prtos_suspend_partition(P2);
    PRINT("Partition %d  %d is suspended\n", P2, retValue);
    if (retValue >= 0) PrintStatus();
    prtos_idle_self();
    retValue = prtos_suspend_partition(P3);
    PRINT("Partition %d  %d is suspended\n", P3, retValue);
    if (retValue >= 0) PrintStatus();

    prtos_idle_self();
    prtos_idle_self();

    retValue = prtos_resume_partition(P2);
    PRINT("Partition %d  %d is resumed \n", P2, retValue);
    if (retValue >= 0) PrintStatus();
    prtos_idle_self();
    retValue = prtos_resume_partition(P3);
    PRINT("Partition %d  %d is resumed \n", P3, retValue);
    if (retValue >= 0) PrintStatus();
    prtos_idle_self();
    prtos_idle_self();

    retValue = prtos_halt_partition(P2);
    PRINT("Partition %d  %d is halted \n", P2, retValue);
    if (retValue >= 0) PrintStatus();
    prtos_idle_self();
    retValue = prtos_halt_partition(P3);
    PRINT("Partition %d  %d is halted \n", P3, retValue);
    if (retValue >= 0) PrintStatus();
    prtos_idle_self();

    retValue = prtos_reset_partition(P2, PRTOS_WARM_RESET, 0);
    PRINT("Partition %d  %d is restarted \n", P2, retValue);
    if (retValue >= 0) PrintStatus();
    prtos_idle_self();
    retValue = prtos_reset_partition(P3, PRTOS_WARM_RESET, 0);
    PRINT("Partition %d  %d is restarted \n", P3, retValue);
    if (retValue >= 0) PrintStatus();
    prtos_idle_self();

    PRINT("Verification Passed\n");
    PRINT("Halting System ...\n");
    retValue = prtos_halt_system();
}
