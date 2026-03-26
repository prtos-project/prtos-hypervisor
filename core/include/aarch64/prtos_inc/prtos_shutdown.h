#ifndef __PRTOS_SHUTDOWN_H__
#define __PRTOS_SHUTDOWN_H__

#include <prtos_compiler.h>

/* opt_noreboot: If true, machine will need manual reset on error. */
extern bool opt_noreboot;

void noreturn hwdom_shutdown(u8 reason);

void noreturn machine_restart(unsigned int delay_millisecs);
void noreturn machine_halt(void);

#endif /* __PRTOS_SHUTDOWN_H__ */
