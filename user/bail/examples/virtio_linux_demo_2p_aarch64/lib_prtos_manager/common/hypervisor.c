/*
 * FILE: hypervisor.c
 *
 * PRTOS Partition Manager - Partition & System control commands
 *
 */

#include "common.h"
#include "prtos_manager.h"

int CmdList(char *line)
{
    int i, ret;
    prtos_part_status_t status;
    int self = prtos_hv_get_partition_self();

    (void)line;

    prtosprintf("List of Partitions:\n");
    for (i = 0; ; i++) {
        ret = prtos_hv_get_partition_status(i, &status);
        if (ret < 0)
            break;

        prtosprintf("%s Partition%d: 0x%x magic 0x%x state 0x%x \"%s\"\n",
                   (i == self) ? "*" : " ",
                   i, 0, 0, status.state, StateToStr(status.state));
    }
    if (i > 0)
        nopart = i - 1;

    return 0;
}

int CmdPartition(char *line)
{
    int narg, ret, partid = 0;
    char *arg[4];
    char *operlist[] = {"halt", "reset", "resume", "status", "suspend"};
    char *resetlist[] = {"cold", "warm"};
    prtos_part_status_t status;
    int resetStatus = 0;

    narg = SplitLine(line, arg, NELEM(arg), 1);
    if (narg <= 0)
        return 0;

    partid = atoi(arg[1]);
    if (CheckPartition(partid, CmdPartition) < 0)
        return 0;

    switch (GetCommandIndex(arg[0], operlist, NELEM(operlist))) {
    case 0: /* halt */
        ret = prtos_hv_halt_partition(partid);
        break;
    case 1: /* reset */
        if (narg == 3)
            resetStatus = atoi(arg[3]);
        switch (GetCommandIndex(arg[2], resetlist, NELEM(resetlist))) {
        case 0:
            ret = prtos_hv_reset_partition(partid, PRTOS_COLD_RESET, resetStatus);
            break;
        case 1:
            ret = prtos_hv_reset_partition(partid, PRTOS_WARM_RESET, resetStatus);
            break;
        default:
            ret = prtos_hv_reset_partition(partid, PRTOS_WARM_RESET, resetStatus);
            break;
        }
        break;
    case 2: /* resume */
        ret = prtos_hv_resume_partition(partid);
        break;
    case 3: /* status */
        ret = prtos_hv_get_partition_status(partid, &status);
        if (ret >= 0) {
            prtosprintf("execClock: %lld state: %d virqs: %lld\n"
                       "resetCounter: %d resetStatus: %d\n",
                       (long long)status.exec_clock, status.state,
                       (long long)status.num_of_virqs,
                       status.reset_counter, status.reset_status);
        }
        break;
    case 4: /* suspend */
        ret = prtos_hv_suspend_partition(partid);
        break;
    default:
        return -1;
    }

    return ret;
}

int CmdPlan(char *line)
{
    int narg, ret, planid = 0;
    prtos_plan_status_t status;
    char *arg[1];

    narg = SplitLine(line, arg, NELEM(arg), 1);
    if (narg < 0) {
        ret = prtos_hv_get_plan_status(&status);
        if (ret >= 0) {
            prtosprintf("plan next: %d current: %d prev: %d switchTime: %lld\n",
                       status.next, status.current, status.prev,
                       (long long)status.switch_time);
        }
        return ret;
    }

    planid = atoi(arg[0]);
    ret = prtos_hv_set_plan(planid);
    return ret;
}

int CmdWriteConsole(char *line)
{
    int narg, ret;
    char *arg[1];

    narg = SplitLine(line, arg, NELEM(arg), 1);
    if (narg < 0)
        return 0;

    ret = prtos_hv_write_console(arg[0], strlen(arg[0]));
    return ret;
}
