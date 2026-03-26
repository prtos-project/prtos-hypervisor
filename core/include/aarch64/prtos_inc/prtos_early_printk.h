/*
 * printk() for use before the console is initialized
 */
#ifndef __PRTOS_EARLY_PRINTK_H__
#define __PRTOS_EARLY_PRINTK_H__

#include <prtos_types.h>

#ifdef CONFIG_EARLY_PRINTK
void early_puts(const char *s, size_t nr);
#else
#define early_puts NULL
#endif

#endif
/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
