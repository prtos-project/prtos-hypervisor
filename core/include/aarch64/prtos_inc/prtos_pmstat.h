#ifndef __PRTOS_PMSTAT_H_
#define __PRTOS_PMSTAT_H_

#include <prtos_types.h>
#include <public_platform.h> /* for struct prtos_processor_power */
#include <public_sysctl.h>   /* for struct pm_cx_stat */

int set_px_pminfo(uint32_t acpi_id, struct prtos_processor_performance *perf);
long set_cx_pminfo(uint32_t acpi_id, struct prtos_processor_power *power);
uint32_t pmstat_get_cx_nr(unsigned int cpu);
int pmstat_get_cx_stat(unsigned int cpu, struct pm_cx_stat *stat);
int pmstat_reset_cx_stat(unsigned int cpu);

int do_get_pm_info(struct prtos_sysctl_get_pmstat *op);
int do_pm_op(struct prtos_sysctl_pm_op *op);

#endif /* __PRTOS_PMSTAT_H_ */
