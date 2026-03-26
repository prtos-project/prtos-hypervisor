/*
 * FILE: kthread.c
 *
 * Kernel and Guest context
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <kthread.h>
#include <physmm.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <virtmm.h>
#include <vmmap.h>
#include <prtosef.h>
#include <arch/prtos_def.h>

void setup_pct_mm(partition_control_table_t *part_ctrl_table, kthread_t *k) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    part_ctrl_table->arch._ARCH_PTDL1_REG = k->ctrl.g->karch.ptd_level_1;
#endif
}
