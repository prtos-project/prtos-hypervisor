#ifndef __PRTOS_PV_CONSOLE_H__
#define __PRTOS_PV_CONSOLE_H__

#include <prtos_serial.h>

#ifdef CONFIG_PRTOS_GUEST

void pv_console_init(void);
void pv_console_set_rx_handler(serial_rx_fn fn);
void pv_console_init_postirq(void);
void pv_console_puts(const char *buf, size_t nr);
size_t pv_console_rx(void);
evtchn_port_t pv_console_evtchn(void);

#else

static inline void pv_console_init(void) {}
static inline void pv_console_set_rx_handler(serial_rx_fn fn) { }
static inline void pv_console_init_postirq(void) { }
static inline void pv_console_puts(const char *buf, size_t nr) { }
static inline size_t pv_console_rx(void) { return 0; }

#endif /* !CONFIG_PRTOS_GUEST */
#endif /* __PRTOS_PV_CONSOLE_H__ */
/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
