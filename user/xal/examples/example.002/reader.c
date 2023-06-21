#include <stdio.h>  // stdio services provided by the libxal library
#include <prtos.h>  // hypercall services provided by the libprtos library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

#define HALTED 3

static void print_hm_log(prtos_hm_log_t *hm_log) {
    printf("part_Id: 0x%x eventId: 0x%x timeStamp: %lld\n", hm_log->op_code_lo & HMLOG_OPCODE_PARTID_MASK,
           (hm_log->op_code_lo & HMLOG_OPCODE_EVENT_MASK) >> HMLOG_OPCODE_EVENT_BIT, hm_log->timestamp);
}

void partition_main(void) {
    prtos_part_status_t partStatus;
    prtos_hm_status_t hmStatus;
    prtos_hm_log_t hm_log;

    prtos_idle_self();

    PRINT(" --------- Health Monitor Log ---------------\n");
    while (1) {
        prtos_hm_status(&hmStatus);
        while (prtos_hm_read(&hm_log)) {
            PRINT("Log => ");
            print_hm_log(&hm_log);
        }

        prtos_get_partition_status(1, &partStatus);
        if (partStatus.state == HALTED) {
            prtos_halt_partition(PRTOS_PARTITION_SELF);
        }
        prtos_idle_self();
    }
    PRINT("--------- Health Monitor Log ---------------\n");
}
