/******************************************************************************
 * watchdog.h
 *
 * Common watchdog code
 */

#ifndef __PRTOS_WATCHDOG_H__
#define __PRTOS_WATCHDOG_H__

#include <prtos_types.h>

#ifdef CONFIG_WATCHDOG

/* Try to set up a watchdog. */
void watchdog_setup(void);

/* Enable the watchdog. */
void watchdog_enable(void);

/* Disable the watchdog. */
void watchdog_disable(void);

/* Is the watchdog currently enabled. */
bool watchdog_enabled(void);

#else

#define watchdog_setup() ((void)0)
#define watchdog_enable() ((void)0)
#define watchdog_disable() ((void)0)
#define watchdog_enabled() ((void)0)

#endif

#endif /* __PRTOS_WATCHDOG_H__ */
