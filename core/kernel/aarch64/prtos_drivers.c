/* PRTOS drivers - consolidated */
/* === BEGIN INLINED: serial.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * serial.c
 * 
 * Framework for serial device drivers.
 * 
 * Copyright (c) 2003-2008, K A Fraser
 */

#include <prtos_delay.h>
#include <prtos_init.h>
#include <prtos_mm.h>
#include <prtos_param.h>
#include <prtos_serial.h>
#include <prtos_cache.h>

#include <asm_processor.h>

/* Never drop characters, even if the async transmit buffer fills. */
/* #define SERIAL_NEVER_DROP_CHARS 1 */

unsigned int __ro_after_init serial_txbufsz = CONFIG_SERIAL_TX_BUFSIZE;
size_param("serial_tx_buffer", serial_txbufsz);

#define mask_serial_rxbuf_idx(_i) ((_i)&(serial_rxbufsz-1))
#define mask_serial_txbuf_idx(_i) ((_i)&(serial_txbufsz-1))

static struct serial_port com[SERHND_IDX + 1] = {
    [0 ... SERHND_IDX] = {
        .rx_lock = SPIN_LOCK_UNLOCKED,
        .tx_lock = SPIN_LOCK_UNLOCKED
    }
};

static bool __read_mostly post_irq;

static inline void serial_start_tx(struct serial_port *port)
{
    if ( port->driver->start_tx != NULL )
        port->driver->start_tx(port);
}

static inline void serial_stop_tx(struct serial_port *port)
{
    if ( port->driver->stop_tx != NULL )
        port->driver->stop_tx(port);
}



static void __serial_putc(struct serial_port *port, char c)
{
    if ( (port->txbuf != NULL) && !port->sync )
    {
        /* Interrupt-driven (asynchronous) transmitter. */

        if ( port->tx_quench )
        {
            /* Buffer filled and we are dropping characters. */
            if ( (port->txbufp - port->txbufc) > (serial_txbufsz / 2) )
                return;
            port->tx_quench = 0;
        }

        if ( (port->txbufp - port->txbufc) == serial_txbufsz )
        {
            if ( port->tx_log_everything )
            {
                /* Buffer is full: we spin waiting for space to appear. */
                int n;

                while ( (n = port->driver->tx_ready(port)) == 0 )
                    cpu_relax();
                if ( n > 0 )
                {
                    /* Enable TX before sending chars */
                    serial_start_tx(port);
                    while ( n-- )
                        port->driver->putc(
                            port,
                            port->txbuf[mask_serial_txbuf_idx(port->txbufc++)]);
                    port->txbuf[mask_serial_txbuf_idx(port->txbufp++)] = c;
                }
            }
            else
            {
                /* Buffer is full: drop chars until buffer is half empty. */
                port->tx_quench = 1;
            }
            return;
        }

        if ( ((port->txbufp - port->txbufc) == 0) &&
             port->driver->tx_ready(port) > 0 )
        {
            /* Enable TX before sending chars */
            serial_start_tx(port);
            /* Buffer and UART FIFO are both empty, and port is available. */
            port->driver->putc(port, c);
        }
        else
        {
            /* Normal case: buffer the character. */
            port->txbuf[mask_serial_txbuf_idx(port->txbufp++)] = c;
        }
    }
    else if ( port->driver->tx_ready )
    {
        int n;

        /* Synchronous finite-capacity transmitter. */
        while ( !(n = port->driver->tx_ready(port)) )
            cpu_relax();
        if ( n > 0 )
        {
            /* Enable TX before sending chars */
            serial_start_tx(port);
            port->driver->putc(port, c);
        }
    }
    else
    {
        /* Simple synchronous transmitter. */
        serial_start_tx(port);
        port->driver->putc(port, c);
    }
}

void serial_puts(int handle, const char *s, size_t nr)
{
    struct serial_port *port;
    unsigned long flags;

    if ( handle == -1 )
        return;

    port = &com[handle & SERHND_IDX];
    if ( !port->driver || !port->driver->putc )
        return;

    spin_lock_irqsave(&port->tx_lock, flags);

    for ( ; nr > 0; nr--, s++ )
    {
        char c = *s;

        if ( (c == '\n') && (handle & SERHND_COOKED) )
            __serial_putc(port, '\r' | ((handle & SERHND_HI) ? 0x80 : 0x00));

        if ( handle & SERHND_HI )
            c |= 0x80;
        else if ( handle & SERHND_LO )
            c &= 0x7f;

        __serial_putc(port, c);
    }

    if ( port->driver->flush )
        port->driver->flush(port);

    spin_unlock_irqrestore(&port->tx_lock, flags);
}

int __init serial_parse_handle(const char *conf)
{
    int handle, flags = 0;

    if ( !strncmp(conf, "dbgp", 4) && (!conf[4] || conf[4] == ',') )
    {
        handle = SERHND_DBGP;
        goto common;
    }

    if ( !strncmp(conf, "ehci", 4) && (!conf[4] || conf[4] == ',') )
    {
        handle = SERHND_DBGP;
        goto common;
    }

    if ( !strncmp(conf, "xhci", 4) && (!conf[4] || conf[4] == ',') )
    {
        handle = SERHND_XHCI;
        goto common;
    }

    if ( !strncmp(conf, "dtuart", 6) )
    {
        handle = SERHND_DTUART;
        goto common;
    }

    if ( strncmp(conf, "com", 3) )
        goto fail;

    switch ( conf[3] )
    {
    case '1':
        handle = SERHND_COM1;
        break;
    case '2':
        handle = SERHND_COM2;
        break;
    default:
        goto fail;
    }

    if ( conf[4] == 'H' )
        flags |= SERHND_HI;
    else if ( conf[4] == 'L' )
        flags |= SERHND_LO;

 common:
    if ( !com[handle].driver )
        goto fail;

    if ( !post_irq )
        com[handle].state = serial_parsed;
    else if ( com[handle].state != serial_initialized )
    {
        if ( com[handle].driver->init_postirq )
            com[handle].driver->init_postirq(&com[handle]);
        com[handle].state = serial_initialized;
    }

    return handle | flags | SERHND_COOKED;

 fail:
    return -1;
}

void __init serial_set_rx_handler(int handle, serial_rx_fn fn)
{
    struct serial_port *port;
    unsigned long flags;

    if ( handle == -1 )
        return;

    port = &com[handle & SERHND_IDX];

    spin_lock_irqsave(&port->rx_lock, flags);

    if ( port->rx != NULL )
        goto fail;

    if ( handle & SERHND_LO )
    {
        if ( port->rx_lo != NULL )
            goto fail;
        port->rx_lo = fn;        
    }
    else if ( handle & SERHND_HI )
    {
        if ( port->rx_hi != NULL )
            goto fail;
        port->rx_hi = fn;
    }
    else
    {
        if ( (port->rx_hi != NULL) || (port->rx_lo != NULL) )
            goto fail;
        port->rx = fn;
    }

    spin_unlock_irqrestore(&port->rx_lock, flags);
    return;

 fail:
    spin_unlock_irqrestore(&port->rx_lock, flags);
    printk("ERROR: Conflicting receive handlers for COM%d\n", 
           handle & SERHND_IDX);
}

void serial_force_unlock(int handle)
{
    struct serial_port *port;

    if ( handle == -1 )
        return;

    port = &com[handle & SERHND_IDX];

    spin_lock_init(&port->rx_lock);
    spin_lock_init(&port->tx_lock);

    serial_start_sync(handle);
}

void serial_start_sync(int handle)
{
    struct serial_port *port;
    unsigned long flags;

    if ( handle == -1 )
        return;
    
    port = &com[handle & SERHND_IDX];

    spin_lock_irqsave(&port->tx_lock, flags);

    if ( port->sync++ == 0 )
    {
        while ( (port->txbufp - port->txbufc) != 0 )
        {
            int n;

            while ( !(n = port->driver->tx_ready(port)) )
                cpu_relax();
            if ( n < 0 )
                /* port is unavailable and might not come up until reenabled by
                   dom0, we can't really do proper sync */
                break;
            serial_start_tx(port);
            port->driver->putc(
                port, port->txbuf[mask_serial_txbuf_idx(port->txbufc++)]);
        }
        if ( port->driver->flush )
            port->driver->flush(port);
    }

    spin_unlock_irqrestore(&port->tx_lock, flags);
}

void serial_end_sync(int handle)
{
    struct serial_port *port;
    unsigned long flags;

    if ( handle == -1 )
        return;
    
    port = &com[handle & SERHND_IDX];

    spin_lock_irqsave(&port->tx_lock, flags);

    port->sync--;

    spin_unlock_irqrestore(&port->tx_lock, flags);
}

void serial_start_log_everything(int handle)
{
    struct serial_port *port;
    unsigned long flags;

    if ( handle == -1 )
        return;
    
    port = &com[handle & SERHND_IDX];

    spin_lock_irqsave(&port->tx_lock, flags);
    port->tx_log_everything++;
    port->tx_quench = 0;
    spin_unlock_irqrestore(&port->tx_lock, flags);
}

void serial_end_log_everything(int handle)
{
    struct serial_port *port;
    unsigned long flags;

    if ( handle == -1 )
        return;
    
    port = &com[handle & SERHND_IDX];

    spin_lock_irqsave(&port->tx_lock, flags);
    port->tx_log_everything--;
    spin_unlock_irqrestore(&port->tx_lock, flags);
}

void __init serial_init_preirq(void)
{
    int i;
    for ( i = 0; i < ARRAY_SIZE(com); i++ )
        if ( com[i].driver && com[i].driver->init_preirq )
            com[i].driver->init_preirq(&com[i]);
}


void __init serial_init_postirq(void)
{
    int i;
    for ( i = 0; i < ARRAY_SIZE(com); i++ )
        if ( com[i].state == serial_parsed )
        {
            if ( com[i].driver->init_postirq )
                com[i].driver->init_postirq(&com[i]);
            com[i].state = serial_initialized;
        }
    post_irq = 1;
}

void __init serial_endboot(void)
{
    int i;
    for ( i = 0; i < ARRAY_SIZE(com); i++ )
        if ( com[i].driver && com[i].driver->endboot )
            com[i].driver->endboot(&com[i]);
}

int __init serial_irq(int idx)
{
    if ( (idx >= 0) && (idx < ARRAY_SIZE(com)) &&
         com[idx].driver && com[idx].driver->irq )
        return com[idx].driver->irq(&com[idx]);

    return -1;
}

const struct vuart_info *serial_vuart_info(int idx)
{
    if ( (idx >= 0) && (idx < ARRAY_SIZE(com)) &&
         com[idx].driver && com[idx].driver->vuart_info )
        return com[idx].driver->vuart_info(&com[idx]);

    return NULL;
}





/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: serial.c === */
/* === BEGIN INLINED: console.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * console.c
 * 
 * Emergency console I/O for PRTOS and the domain-0 guest OS.
 * 
 * Copyright (c) 2002-2004, K A Fraser.
 *
 * Added printf_ratelimit
 *     Taken from Linux - Author: Andi Kleen (net_ratelimit)
 *     Ported to PRTOS - Steven Rostedt - Red Hat
 */

#include <prtos_version.h>
#include <prtos_lib.h>
#include <prtos_init.h>
#include <prtos_event.h>
#include <prtos_console.h>
#include <prtos_param.h>
#include <prtos_serial.h>
#include <prtos_softirq.h>
#include <prtos_keyhandler.h>
#include <prtos_guest_access.h>
#include <prtos_watchdog.h>
#include <prtos_shutdown.h>
#include <prtos_video.h>
#include <prtos_kexec.h>
#include <prtos_ctype.h>
#include <prtos_warning.h>
#include <asm_div64.h>
#include <prtos_hypercall.h> /* for do_console_io */
#include <prtos_early_printk.h>
#include <prtos_warning.h>
#include <prtos_pv_console.h>
#include <asm_setup.h>

#ifdef CONFIG_X86
#include <prtos/consoled.h>
#include <asm/guest.h>
#endif
#ifdef CONFIG_SBSA_VUART_CONSOLE
#include <asm_vpl011.h>
#endif

/* console: comma-separated list of console outputs. */
static char __initdata opt_console[30] = OPT_CONSOLE_STR;
string_param("console", opt_console);

/* conswitch: a character pair controlling console switching. */
/* Char 1: CTRL+<char1> is used to switch console input between PRTOS and DOM0 */
/* Char 2: If this character is 'x', then do not auto-switch to DOM0 when it */
/*         boots. Any other value, or omitting the char, enables auto-switch */
static char __read_mostly opt_conswitch[3] = "a";
string_runtime_param("conswitch", opt_conswitch);

/* sync_console: force synchronous console output (useful for debugging). */
static bool __initdata opt_sync_console;
boolean_param("sync_console", opt_sync_console);
static const char __initconst warning_sync_console[] =
    "WARNING: CONSOLE OUTPUT IS SYNCHRONOUS\n"
    "This option is intended to aid debugging of PRTOS by ensuring\n"
    "that all output is synchronously delivered on the serial line.\n"
    "However it can introduce SIGNIFICANT latencies and affect\n"
    "timekeeping. It is NOT recommended for production use!\n";

/* console_to_ring: send guest (incl. dom 0) console data to console ring. */
static bool __read_mostly opt_console_to_ring;
boolean_param("console_to_ring", opt_console_to_ring);

/* console_timestamps: include a timestamp prefix on every PRTOS console line. */
enum con_timestamp_mode
{
    TSM_NONE,          /* No timestamps */
    TSM_DATE,          /* [YYYY-MM-DD HH:MM:SS] */
    TSM_DATE_MS,       /* [YYYY-MM-DD HH:MM:SS.mmm] */
    TSM_BOOT,          /* [SSSSSS.uuuuuu] */
    TSM_RAW,           /* [XXXXXXXXXXXXXXXX] */
};

static enum con_timestamp_mode __read_mostly opt_con_timestamp_mode = TSM_NONE;

#ifdef CONFIG_HYPFS
static const char con_timestamp_mode_2_string[][7] = {
    [TSM_NONE] = "none",
    [TSM_DATE] = "date",
    [TSM_DATE_MS] = "datems",
    [TSM_BOOT] = "boot",
    [TSM_RAW] = "raw",
};

static void cf_check con_timestamp_mode_upd(struct param_hypfs *par)
{
    const char *val = con_timestamp_mode_2_string[opt_con_timestamp_mode];

    custom_runtime_set_var_sz(par, val, 7);
}
#else
#define con_timestamp_mode_upd(par)
#endif

static int cf_check parse_console_timestamps(const char *s);
custom_runtime_param("console_timestamps", parse_console_timestamps,
                     con_timestamp_mode_upd);

/* conring_size: allows a large console ring than default (16kB). */
static uint32_t __initdata opt_conring_size;
size_param("conring_size", opt_conring_size);

#define _CONRING_SIZE 16384
#define CONRING_IDX_MASK(i) ((i)&(conring_size-1))
static char __initdata _conring[_CONRING_SIZE];
static char *__read_mostly conring = _conring;
static uint32_t __read_mostly conring_size = _CONRING_SIZE;
static uint32_t conringc, conringp;

static int __read_mostly sercon_handle = -1;

#ifdef CONFIG_X86
/* Tristate: 0 disabled, 1 user enabled, -1 default enabled */
int8_t __read_mostly opt_console_prtos; /* console=prtos */
#endif

static DEFINE_RSPINLOCK(console_lock);

/*
 * To control the amount of printing, thresholds are added.
 * These thresholds correspond to the PRTOSLOG logging levels.
 * There's an upper and lower threshold for non-guest messages and for
 * guest-provoked messages.  This works as follows, for a given log level L:
 *
 * L < lower_threshold                     : always logged
 * lower_threshold <= L < upper_threshold  : rate-limited logging
 * upper_threshold <= L                    : never logged
 *
 * Note, in the above algorithm, to disable rate limiting simply make
 * the lower threshold equal to the upper.
 */
#ifdef NDEBUG
#define PRTOSLOG_UPPER_THRESHOLD       3 /* Do not print DEBUG  */
#define PRTOSLOG_LOWER_THRESHOLD       3 /* Always print INFO, ERR and WARNING */
#define PRTOSLOG_GUEST_UPPER_THRESHOLD 2 /* Do not print INFO and DEBUG  */
#define PRTOSLOG_GUEST_LOWER_THRESHOLD 0 /* Rate-limit ERR and WARNING   */
#else
#define PRTOSLOG_UPPER_THRESHOLD       4 /* Do not discard anything      */
#define PRTOSLOG_LOWER_THRESHOLD       4 /* Print everything             */
#define PRTOSLOG_GUEST_UPPER_THRESHOLD 4 /* Do not discard anything      */
#define PRTOSLOG_GUEST_LOWER_THRESHOLD 4 /* Print everything             */
#endif
/*
 * The PRTOSLOG_DEFAULT is the default given to printks that
 * do not have any print level associated with them.
 */
#define PRTOSLOG_DEFAULT       2 /* PRTOSLOG_INFO */
#define PRTOSLOG_GUEST_DEFAULT 1 /* PRTOSLOG_WARNING */

static int __read_mostly prtoslog_upper_thresh = PRTOSLOG_UPPER_THRESHOLD;
static int __read_mostly prtoslog_lower_thresh = PRTOSLOG_LOWER_THRESHOLD;
static int __read_mostly prtoslog_guest_upper_thresh =
    PRTOSLOG_GUEST_UPPER_THRESHOLD;
static int __read_mostly prtoslog_guest_lower_thresh =
    PRTOSLOG_GUEST_LOWER_THRESHOLD;

static int cf_check parse_loglvl(const char *s);
static int cf_check parse_guest_loglvl(const char *s);

#ifdef CONFIG_HYPFS
#define LOGLVL_VAL_SZ 16
static char prtoslog_val[LOGLVL_VAL_SZ];
static char prtoslog_guest_val[LOGLVL_VAL_SZ];

static void prtoslog_update_val(int lower, int upper, char *val)
{
    static const char * const lvl2opt[] =
        { "none", "error", "warning", "info", "all" };

    snprintf(val, LOGLVL_VAL_SZ, "%s/%s", lvl2opt[lower], lvl2opt[upper]);
}

static void __init cf_check prtoslog_init(struct param_hypfs *par)
{
    prtoslog_update_val(prtoslog_lower_thresh, prtoslog_upper_thresh, prtoslog_val);
    custom_runtime_set_var(par, prtoslog_val);
}

static void __init cf_check prtoslog_guest_init(struct param_hypfs *par)
{
    prtoslog_update_val(prtoslog_guest_lower_thresh, prtoslog_guest_upper_thresh,
                      prtoslog_guest_val);
    custom_runtime_set_var(par, prtoslog_guest_val);
}
#else
#define prtoslog_val       NULL
#define prtoslog_guest_val NULL

static void prtoslog_update_val(int lower, int upper, char *val)
{
}
#endif

/*
 * <lvl> := none|error|warning|info|debug|all
 * loglvl=<lvl_print_always>[/<lvl_print_ratelimit>]
 *  <lvl_print_always>: log level which is always printed
 *  <lvl_print_rlimit>: log level which is rate-limit printed
 * Similar definitions for guest_loglvl, but applies to guest tracing.
 * Defaults: loglvl=warning ; guest_loglvl=none/warning
 */
custom_runtime_param("loglvl", parse_loglvl, prtoslog_init);
custom_runtime_param("guest_loglvl", parse_guest_loglvl, prtoslog_guest_init);

static atomic_t print_everything = ATOMIC_INIT(0);

#define ___parse_loglvl(s, ps, lvlstr, lvlnum)          \
    if ( !strncmp((s), (lvlstr), strlen(lvlstr)) ) {    \
        *(ps) = (s) + strlen(lvlstr);                   \
        return (lvlnum);                                \
    }

static int __parse_loglvl(const char *s, const char **ps)
{
    ___parse_loglvl(s, ps, "none",    0);
    ___parse_loglvl(s, ps, "error",   1);
    ___parse_loglvl(s, ps, "warning", 2);
    ___parse_loglvl(s, ps, "info",    3);
    ___parse_loglvl(s, ps, "debug",   4);
    ___parse_loglvl(s, ps, "all",     4);
    return 2; /* sane fallback */
}

static int _parse_loglvl(const char *s, int *lower, int *upper, char *val)
{
    *lower = *upper = __parse_loglvl(s, &s);
    if ( *s == '/' )
        *upper = __parse_loglvl(s+1, &s);
    if ( *upper < *lower )
        *upper = *lower;

    prtoslog_update_val(*lower, *upper, val);

    return *s ? -EINVAL : 0;
}

static int cf_check parse_loglvl(const char *s)
{
    int ret;

    ret = _parse_loglvl(s, &prtoslog_lower_thresh, &prtoslog_upper_thresh,
                        prtoslog_val);
    custom_runtime_set_var(param_2_parfs(parse_loglvl), prtoslog_val);

    return ret;
}

static int cf_check parse_guest_loglvl(const char *s)
{
    int ret;

    ret = _parse_loglvl(s, &prtoslog_guest_lower_thresh,
                        &prtoslog_guest_upper_thresh, prtoslog_guest_val);
    custom_runtime_set_var(param_2_parfs(parse_guest_loglvl),
                           prtoslog_guest_val);

    return ret;
}

static const char *loglvl_str(int lvl)
{
    switch ( lvl )
    {
    case 0: return "Nothing";
    case 1: return "Errors";
    case 2: return "Errors and warnings";
    case 3: return "Errors, warnings and info";
    case 4: return "All";
    }
    return "???";
}

static int *__read_mostly upper_thresh_adj = &prtoslog_upper_thresh;
static int *__read_mostly lower_thresh_adj = &prtoslog_lower_thresh;
static const char *__read_mostly thresh_adj = "standard";

static void cf_check do_toggle_guest(unsigned char key, bool unused)
{
    if ( upper_thresh_adj == &prtoslog_upper_thresh )
    {
        upper_thresh_adj = &prtoslog_guest_upper_thresh;
        lower_thresh_adj = &prtoslog_guest_lower_thresh;
        thresh_adj = "guest";
    }
    else
    {
        upper_thresh_adj = &prtoslog_upper_thresh;
        lower_thresh_adj = &prtoslog_lower_thresh;
        thresh_adj = "standard";
    }
    printk("'%c' pressed -> %s log level adjustments enabled\n",
           key, thresh_adj);
}

static void do_adj_thresh(unsigned char key)
{
    if ( *upper_thresh_adj < *lower_thresh_adj )
        *upper_thresh_adj = *lower_thresh_adj;
    printk("'%c' pressed -> %s log level: %s (rate limited %s)\n",
           key, thresh_adj, loglvl_str(*lower_thresh_adj),
           loglvl_str(*upper_thresh_adj));
}

static void cf_check do_inc_thresh(unsigned char key, bool unused)
{
    ++*lower_thresh_adj;
    do_adj_thresh(key);
}

static void cf_check do_dec_thresh(unsigned char key, bool unused)
{
    if ( *lower_thresh_adj )
        --*lower_thresh_adj;
    do_adj_thresh(key);
}

/*
 * ********************************************************
 * *************** ACCESS TO CONSOLE RING *****************
 * ********************************************************
 */

static void conring_puts(const char *str, size_t len)
{
    ASSERT(rspin_is_locked(&console_lock));

    while ( len-- )
        conring[CONRING_IDX_MASK(conringp++)] = *str++;

    if ( conringp - conringc > conring_size )
        conringc = conringp - conring_size;
}

long read_console_ring(struct prtos_sysctl_readconsole *op)
{
    PRTOS_GUEST_HANDLE_PARAM(char) str;
    uint32_t idx, len, max, sofar, c, p;

    str   = guest_handle_cast(op->buffer, char),
    max   = op->count;
    sofar = 0;

    c = read_atomic(&conringc);
    p = read_atomic(&conringp);
    if ( op->incremental &&
         (c <= p ? c < op->index && op->index <= p
                 : c < op->index || op->index <= p) )
        c = op->index;

    while ( (c != p) && (sofar < max) )
    {
        idx = CONRING_IDX_MASK(c);
        len = p - c;
        if ( (idx + len) > conring_size )
            len = conring_size - idx;
        if ( (sofar + len) > max )
            len = max - sofar;
        if ( copy_to_guest_offset(str, sofar, &conring[idx], len) )
            return -EFAULT;
        sofar += len;
        c += len;
    }

    if ( op->clear )
    {
        nrspin_lock_irq(&console_lock);
        conringc = p - c > conring_size ? p - conring_size : c;
        nrspin_unlock_irq(&console_lock);
    }

    op->count = sofar;
    op->index = c;

    return 0;
}


/*
 * *******************************************************
 * *************** ACCESS TO SERIAL LINE *****************
 * *******************************************************
 */

/* Characters received over the serial line are buffered for domain 0. */
#define SERIAL_RX_SIZE 128
#define SERIAL_RX_MASK(_i) ((_i)&(SERIAL_RX_SIZE-1))
static char serial_rx_ring[SERIAL_RX_SIZE];
static unsigned int serial_rx_cons, serial_rx_prod;

static void (*serial_steal_fn)(const char *str, size_t nr) = early_puts;

int console_steal(int handle, void (*fn)(const char *str, size_t nr))
{
    if ( (handle == -1) || (handle != sercon_handle) )
        return 0;

    if ( serial_steal_fn != NULL )
        return -EBUSY;

    serial_steal_fn = fn;
    return 1;
}

void console_giveback(int id)
{
    if ( id == 1 )
        serial_steal_fn = NULL;
}

void console_serial_puts(const char *s, size_t nr)
{
    if ( serial_steal_fn != NULL )
        serial_steal_fn(s, nr);
    else
        serial_puts(sercon_handle, s, nr);

    /* Copy all serial output into PV console */
    pv_console_puts(s, nr);
}

static void cf_check dump_console_ring_key(unsigned char key)
{
    uint32_t idx, len, sofar, c;
    unsigned int order;
    char *buf;

    printk("'%c' pressed -> dumping console ring buffer (dmesg)\n", key);

    /* create a buffer in which we'll copy the ring in the correct
       order and NUL terminate */
    order = get_order_from_bytes(conring_size + 1);
    buf = alloc_prtosheap_pages(order, 0);
    if ( buf == NULL )
    {
        printk("unable to allocate memory!\n");
        return;
    }

    c = conringc;
    sofar = 0;
    while ( (c != conringp) )
    {
        idx = CONRING_IDX_MASK(c);
        len = conringp - c;
        if ( (idx + len) > conring_size )
            len = conring_size - idx;
        memcpy(buf + sofar, &conring[idx], len);
        sofar += len;
        c += len;
    }

    console_serial_puts(buf, sofar);
    video_puts(buf, sofar);

    free_prtosheap_pages(buf, order);
}

/*
 * CTRL-<switch_char> changes input direction, rotating among PRTOS, Dom0,
 * and the DomUs started from PRTOS at boot.
 */
#define switch_code (opt_conswitch[0]-'a'+1)
/*
 * console_rx=0 => input to prtos
 * console_rx=1 => input to dom0 (or the sole shim domain)
 * console_rx=N => input to dom(N-1)
 */
static unsigned int __read_mostly console_rx = 0;

#define max_console_rx (max_init_domid + 1)

#ifdef CONFIG_SBSA_VUART_CONSOLE
/* Make sure to rcu_unlock_domain after use */
struct domain *console_input_domain(void)
{
    if ( console_rx == 0 )
            return NULL;
    return rcu_lock_domain_by_id(console_rx - 1);
}
#endif

static void switch_serial_input(void)
{
    unsigned int next_rx = console_rx;

    /*
     * Rotate among PRTOS, dom0 and boot-time created domUs while skipping
     * switching serial input to non existing domains.
     */
    for ( ; ; )
    {
        domid_t domid;
        struct domain *d;

        if ( next_rx++ >= max_console_rx )
        {
            console_rx = 0;
            printk("*** Serial input to PRTOS");
            break;
        }

#ifdef CONFIG_PV_SHIM
        if ( next_rx == 1 )
            domid = get_initial_domain_id();
        else
#endif
            domid = next_rx - 1;
        d = rcu_lock_domain_by_id(domid);
        if ( d )
        {
            rcu_unlock_domain(d);
            console_rx = next_rx;
            printk("*** Serial input to DOM%u", domid);
            break;
        }
    }

    if ( switch_code )
        printk(" (type 'CTRL-%c' three times to switch input)",
               opt_conswitch[0]);
    printk("\n");
}

static void __serial_rx(char c)
{
    switch ( console_rx )
    {
    case 0:
        return handle_keypress(c, false);

    case 1:
        /*
         * Deliver input to the hardware domain buffer, unless it is
         * already full.
         */
        if ( (serial_rx_prod - serial_rx_cons) != SERIAL_RX_SIZE )
            serial_rx_ring[SERIAL_RX_MASK(serial_rx_prod++)] = c;

        /*
         * Always notify the hardware domain: prevents receive path from
         * getting stuck.
         */
        send_global_virq(VIRQ_CONSOLE);
        break;

#ifdef CONFIG_SBSA_VUART_CONSOLE
    default:
    {
        struct domain *d = rcu_lock_domain_by_id(console_rx - 1);

        /*
         * If we have a properly initialized vpl011 console for the
         * domain, without a full PV ring to Dom0 (in that case input
         * comes from the PV ring), then send the character to it.
         */
        if ( d != NULL &&
             !d->arch.vpl011.backend_in_domain &&
             d->arch.vpl011.backend.prtos != NULL )
            vpl011_rx_char_prtos(d, c);
        else
            printk("Cannot send chars to Dom%d: no UART available\n",
                   console_rx - 1);

        if ( d != NULL )
            rcu_unlock_domain(d);

        break;
    }
#endif
    }

#ifdef CONFIG_X86
    if ( pv_shim && pv_console )
        consoled_guest_tx(c);
#endif
}

static void cf_check serial_rx(char c)
{
    static int switch_code_count = 0;

    if ( switch_code && (c == switch_code) )
    {
        /* We eat CTRL-<switch_char> in groups of 3 to switch console input. */
        if ( ++switch_code_count == 3 )
        {
            switch_serial_input();
            switch_code_count = 0;
        }
        return;
    }

    for ( ; switch_code_count != 0; switch_code_count-- )
        __serial_rx(switch_code);

    /* Finally process the just-received character. */
    __serial_rx(c);
}

static void cf_check notify_dom0_con_ring(void *unused)
{
    send_global_virq(VIRQ_CON_RING);
}
static DECLARE_SOFTIRQ_TASKLET(notify_dom0_con_ring_tasklet,
                               notify_dom0_con_ring, NULL);

#ifdef CONFIG_X86
static inline void prtos_console_write_debug_port(const char *buf, size_t len)
{
    unsigned long tmp;
    asm volatile ( "rep outsb;"
                   : "=&S" (tmp), "=&c" (tmp)
                   : "0" (buf), "1" (len), "d" (PRTOS_HVM_DEBUGCONS_IOPORT) );
}
#endif

static long guest_console_write(PRTOS_GUEST_HANDLE_PARAM(char) buffer,
                                unsigned int count)
{
    char kbuf[128];
    unsigned int kcount = 0;
    struct domain *cd = current->domain;

    while ( count > 0 )
    {
        if ( kcount && hypercall_preempt_check() )
            return hypercall_create_continuation(
                __HYPERVISOR_console_io, "iih",
                CONSOLEIO_write, count, buffer);

        kcount = min((size_t)count, sizeof(kbuf) - 1);
        if ( copy_from_guest(kbuf, buffer, kcount) )
            return -EFAULT;

        if ( is_hardware_domain(cd) )
        {
            /* Use direct console output as it could be interactive */
            nrspin_lock_irq(&console_lock);

            console_serial_puts(kbuf, kcount);
            video_puts(kbuf, kcount);

#ifdef CONFIG_X86
            if ( opt_console_prtos )
            {
                if ( prtos_guest )
                    prtos_hypercall_console_write(kbuf, kcount);
                else
                    prtos_console_write_debug_port(kbuf, kcount);
            }
#endif

            if ( opt_console_to_ring )
            {
                conring_puts(kbuf, kcount);
                tasklet_schedule(&notify_dom0_con_ring_tasklet);
            }

            nrspin_unlock_irq(&console_lock);
        }
        else
        {
            char *kin = kbuf, *kout = kbuf, c;

            /* Strip non-printable characters */
            do
            {
                c = *kin++;
                if ( c == '\n' )
                    break;
                if ( isprint(c) || c == '\t' )
                    *kout++ = c;
            } while ( --kcount > 0 );

            *kout = '\0';
            spin_lock(&cd->pbuf_lock);
            kcount = kin - kbuf;
            if ( c != '\n' &&
                 (cd->pbuf_idx + (kout - kbuf) < (DOMAIN_PBUF_SIZE - 1)) )
            {
                /* buffer the output until a newline */
                memcpy(cd->pbuf + cd->pbuf_idx, kbuf, kout - kbuf);
                cd->pbuf_idx += (kout - kbuf);
            }
            else
            {
                cd->pbuf[cd->pbuf_idx] = '\0';
                guest_printk(cd, PRTOSLOG_G_DEBUG "%s%s\n", cd->pbuf, kbuf);
                cd->pbuf_idx = 0;
            }
            spin_unlock(&cd->pbuf_lock);
        }

        guest_handle_add_offset(buffer, kcount);
        count -= kcount;
    }

    return 0;
}

long do_console_io(
    unsigned int cmd, unsigned int count, PRTOS_GUEST_HANDLE_PARAM(char) buffer)
{
    long rc;
    unsigned int idx, len;

    rc = xsm_console_io(XSM_OTHER, current->domain, cmd);
    if ( rc )
        return rc;

    switch ( cmd )
    {
    case CONSOLEIO_write:
        rc = guest_console_write(buffer, count);
        break;
    case CONSOLEIO_read:
        /*
         * The return value is either the number of characters read or
         * a negative value in case of error. So we need to prevent
         * overlap between the two sets.
         */
        rc = -E2BIG;
        if ( count > INT_MAX )
            break;

        rc = 0;
        while ( (serial_rx_cons != serial_rx_prod) && (rc < count) )
        {
            idx = SERIAL_RX_MASK(serial_rx_cons);
            len = serial_rx_prod - serial_rx_cons;
            if ( (idx + len) > SERIAL_RX_SIZE )
                len = SERIAL_RX_SIZE - idx;
            if ( (rc + len) > count )
                len = count - rc;
            if ( copy_to_guest_offset(buffer, rc, &serial_rx_ring[idx], len) )
            {
                rc = -EFAULT;
                break;
            }
            rc += len;
            serial_rx_cons += len;
        }
        break;
    default:
        rc = -ENOSYS;
        break;
    }

    return rc;
}


/*
 * *****************************************************
 * *************** GENERIC CONSOLE I/O *****************
 * *****************************************************
 */

static bool console_locks_busted;

static void __putstr(const char *str)
{
    size_t len = strlen(str);

    ASSERT(rspin_is_locked(&console_lock));

    console_serial_puts(str, len);
    video_puts(str, len);

#ifdef CONFIG_X86
    if ( opt_console_prtos )
    {
        if ( prtos_guest )
            prtos_hypercall_console_write(str, len);
        else
            prtos_console_write_debug_port(str, len);
    }
#endif

    conring_puts(str, len);

    if ( !console_locks_busted )
        tasklet_schedule(&notify_dom0_con_ring_tasklet);
}

static int printk_prefix_check(char *p, char **pp)
{
    int loglvl = -1;
    int upper_thresh = ACCESS_ONCE(prtoslog_upper_thresh);
    int lower_thresh = ACCESS_ONCE(prtoslog_lower_thresh);

    while ( (p[0] == '<') && (p[1] != '\0') && (p[2] == '>') )
    {
        switch ( p[1] )
        {
        case 'G':
            upper_thresh = ACCESS_ONCE(prtoslog_guest_upper_thresh);
            lower_thresh = ACCESS_ONCE(prtoslog_guest_lower_thresh);
            if ( loglvl == -1 )
                loglvl = PRTOSLOG_GUEST_DEFAULT;
            break;
        case '0' ... '3':
            loglvl = p[1] - '0';
            break;
        }
        p += 3;
    }

    if ( loglvl == -1 )
        loglvl = PRTOSLOG_DEFAULT;

    *pp = p;

    return ((atomic_read(&print_everything) != 0) ||
            (loglvl < lower_thresh) ||
            ((loglvl < upper_thresh) && printk_ratelimit()));
} 

static int cf_check parse_console_timestamps(const char *s)
{
    switch ( parse_bool(s, NULL) )
    {
    case 0:
        opt_con_timestamp_mode = TSM_NONE;
        con_timestamp_mode_upd(param_2_parfs(parse_console_timestamps));
        return 0;
    case 1:
        opt_con_timestamp_mode = TSM_DATE;
        con_timestamp_mode_upd(param_2_parfs(parse_console_timestamps));
        return 0;
    }
    if ( *s == '\0' || /* Compat for old booleanparam() */
         !strcmp(s, "date") )
        opt_con_timestamp_mode = TSM_DATE;
    else if ( !strcmp(s, "datems") )
        opt_con_timestamp_mode = TSM_DATE_MS;
    else if ( !strcmp(s, "boot") )
        opt_con_timestamp_mode = TSM_BOOT;
    else if ( !strcmp(s, "raw") )
        opt_con_timestamp_mode = TSM_RAW;
    else if ( !strcmp(s, "none") )
        opt_con_timestamp_mode = TSM_NONE;
    else
        return -EINVAL;

    con_timestamp_mode_upd(param_2_parfs(parse_console_timestamps));

    return 0;
}

static void printk_start_of_line(const char *prefix)
{
    enum con_timestamp_mode mode = ACCESS_ONCE(opt_con_timestamp_mode);
    struct tm tm;
    char tstr[32];
    uint64_t sec, nsec;

    __putstr(prefix);

    switch ( mode )
    {
    case TSM_DATE:
    case TSM_DATE_MS:
        tm = wallclock_time(&nsec);

        if ( tm.tm_mday == 0 )
            /* nothing */;
        else if ( mode == TSM_DATE )
        {
            snprintf(tstr, sizeof(tstr), "[%04u-%02u-%02u %02u:%02u:%02u] ",
                     1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
            break;
        }
        else
        {
            snprintf(tstr, sizeof(tstr),
                     "[%04u-%02u-%02u %02u:%02u:%02u.%03"PRIu64"] ",
                     1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec, nsec / 1000000);
            break;
        }
        /* fall through */
    case TSM_BOOT:
        sec = NOW();
        nsec = do_div(sec, 1000000000);

        if ( sec | nsec )
        {
            snprintf(tstr, sizeof(tstr), "[%5"PRIu64".%06"PRIu64"] ",
                     sec, nsec / 1000);
            break;
        }
        /* fall through */
    case TSM_RAW:
        snprintf(tstr, sizeof(tstr), "[%016"PRIx64"] ", get_cycles());
        break;

    case TSM_NONE:
    default:
        return;
    }

    __putstr(tstr);
}

static void vprintk_common(const char *prefix, const char *fmt, va_list args)
{
    struct vps {
        bool continued, do_print;
    }            *state;
    static DEFINE_PER_CPU(struct vps, state);
    static char   buf[1024];
    char         *p, *q;
    unsigned long flags;

    /* console_lock can be acquired recursively from __printk_ratelimit(). */
    local_irq_save(flags);
    rspin_lock(&console_lock);
    state = &this_cpu(state);

    (void)vsnprintf(buf, sizeof(buf), fmt, args);

    p = buf;

    while ( (q = strchr(p, '\n')) != NULL )
    {
        *q = '\0';
        if ( !state->continued )
            state->do_print = printk_prefix_check(p, &p);
        if ( state->do_print )
        {
            if ( !state->continued )
                printk_start_of_line(prefix);
            __putstr(p);
            __putstr("\n");
        }
        state->continued = 0;
        p = q + 1;
    }

    if ( *p != '\0' )
    {
        if ( !state->continued )
            state->do_print = printk_prefix_check(p, &p);
        if ( state->do_print )
        {
            if ( !state->continued )
                printk_start_of_line(prefix);
            __putstr(p);
        }
        state->continued = 1;
    }

    rspin_unlock(&console_lock);
    local_irq_restore(flags);
}

void printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintk_common("(PRTOS) ", fmt, args);
    va_end(args);
}

void guest_printk(const struct domain *d, const char *fmt, ...)
{
    va_list args;
    char prefix[16];

    snprintf(prefix, sizeof(prefix), "(d%d) ", d->domain_id);

    va_start(args, fmt);
    vprintk_common(prefix, fmt, args);
    va_end(args);
}

void __init console_init_preirq(void)
{
    char *p;
    int sh;

    serial_init_preirq();

    /* Where should console output go? */
    for ( p = opt_console; p != NULL; p = strchr(p, ',') )
    {
        if ( *p == ',' )
            p++;
        if ( !strncmp(p, "vga", 3) )
            video_init();
        else if ( !strncmp(p, "pv", 2) )
            pv_console_init();
#ifdef CONFIG_X86
        else if ( !strncmp(p, "prtos", 3) )
            opt_console_prtos = 1;
#endif
        else if ( !strncmp(p, "none", 4) )
            continue;
        else if ( (sh = serial_parse_handle(p)) >= 0 )
        {
            sercon_handle = sh;
            serial_steal_fn = NULL;
        }
        else
        {
            char *q = strchr(p, ',');
            if ( q != NULL )
                *q = '\0';
            printk("Bad console= option '%s'\n", p);
            if ( q != NULL )
                *q = ',';
        }
    }

#ifdef CONFIG_X86
    if ( opt_console_prtos == -1 )
        opt_console_prtos = 0;
#endif

    serial_set_rx_handler(sercon_handle, serial_rx);
    pv_console_set_rx_handler(serial_rx);

    /* HELLO WORLD --- start-of-day banner text. */
    nrspin_lock(&console_lock);
    __putstr(prtos_banner());
    nrspin_unlock(&console_lock);
    printk("PRTOS version %d.%d%s (%s@%s) (%s) %s %s\n",
           prtos_major_version(), prtos_minor_version(), prtos_extra_version(),
           prtos_compile_by(), prtos_compile_domain(), prtos_compiler(),
           prtos_build_info(), prtos_compile_date());
    printk("Latest ChangeSet: %s\n", prtos_changeset());

    /* Locate and print the buildid, if applicable. */
    prtos_build_init();

    if ( opt_sync_console )
    {
        serial_start_sync(sercon_handle);
        add_taint(TAINT_SYNC_CONSOLE);
        printk("Console output is synchronous.\n");
        warning_add(warning_sync_console);
    }
}

void __init console_init_ring(void)
{
    char *ring;
    unsigned int i, order, memflags;
    unsigned long flags;

    if ( !opt_conring_size )
        return;

    order = get_order_from_bytes(max(opt_conring_size, conring_size));
    memflags = MEMF_bits(crashinfo_maxaddr_bits);
    while ( (ring = alloc_prtosheap_pages(order, memflags)) == NULL )
    {
        BUG_ON(order == 0);
        order--;
    }
    opt_conring_size = PAGE_SIZE << order;

    nrspin_lock_irqsave(&console_lock, flags);
    for ( i = conringc ; i != conringp; i++ )
        ring[i & (opt_conring_size - 1)] = conring[i & (conring_size - 1)];
    conring = ring;
    smp_wmb(); /* Allow users of console_force_unlock() to see larger buffer. */
    conring_size = opt_conring_size;
    nrspin_unlock_irqrestore(&console_lock, flags);

    printk("Allocated console ring of %u KiB.\n", opt_conring_size >> 10);
}


void __init console_init_postirq(void)
{
    serial_init_postirq();
    pv_console_init_postirq();

    if ( conring != _conring )
        return;

    if ( !opt_conring_size )
        opt_conring_size = num_present_cpus() << (9 + prtoslog_lower_thresh);

    console_init_ring();
}

void __init console_endboot(void)
{
    printk("Std. Loglevel: %s", loglvl_str(prtoslog_lower_thresh));
    if ( prtoslog_upper_thresh != prtoslog_lower_thresh )
        printk(" (Rate-limited: %s)", loglvl_str(prtoslog_upper_thresh));
    printk("\nGuest Loglevel: %s", loglvl_str(prtoslog_guest_lower_thresh));
    if ( prtoslog_guest_upper_thresh != prtoslog_guest_lower_thresh )
        printk(" (Rate-limited: %s)", loglvl_str(prtoslog_guest_upper_thresh));
    printk("\n");

    warning_print();

    video_endboot();

    /*
     * If user specifies so, we fool the switch routine to redirect input
     * straight back to PRTOS. I use this convoluted method so we still print
     * a useful 'how to switch' message.
     */
    if ( opt_conswitch[1] == 'x' )
        console_rx = max_console_rx;

    register_keyhandler('w', dump_console_ring_key,
                        "synchronously dump console ring buffer (dmesg)", 0);
    register_irq_keyhandler('+', &do_inc_thresh,
                            "increase log level threshold", 0);
    register_irq_keyhandler('-', &do_dec_thresh,
                            "decrease log level threshold", 0);
    register_irq_keyhandler('G', &do_toggle_guest,
                            "toggle host/guest log level adjustment", 0);

    /* Serial input is directed to DOM0 by default. */
    switch_serial_input();
}

int __init console_has(const char *device)
{
    char *p;

    for ( p = opt_console; p != NULL; p = strchr(p, ',') )
    {
        if ( *p == ',' )
            p++;
        if ( strncmp(p, device, strlen(device)) == 0 )
            return 1;
    }

    return 0;
}

void console_start_log_everything(void)
{
    serial_start_log_everything(sercon_handle);
    atomic_inc(&print_everything);
}

void console_end_log_everything(void)
{
    serial_end_log_everything(sercon_handle);
    atomic_dec(&print_everything);
}



void console_force_unlock(void)
{
    watchdog_disable();
    spin_debug_disable();
    rspin_lock_init(&console_lock);
    serial_force_unlock(sercon_handle);
    console_locks_busted = 1;
    console_start_sync();
}

void console_start_sync(void)
{
    atomic_inc(&print_everything);
    serial_start_sync(sercon_handle);
}

void console_end_sync(void)
{
    serial_end_sync(sercon_handle);
    atomic_dec(&print_everything);
}

/*
 * printk rate limiting, lifted from Linux.
 *
 * This enforces a rate limit: not more than one kernel message
 * every printk_ratelimit_ms (millisecs).
 */
int __printk_ratelimit(int ratelimit_ms, int ratelimit_burst)
{
    static DEFINE_SPINLOCK(ratelimit_lock);
    static unsigned long toks = 10 * 5 * 1000;
    static unsigned long last_msg;
    static int missed;
    unsigned long flags;
    unsigned long long now = NOW(); /* ns */
    unsigned long ms;

    do_div(now, 1000000);
    ms = (unsigned long)now;

    spin_lock_irqsave(&ratelimit_lock, flags);
    toks += ms - last_msg;
    last_msg = ms;
    if ( toks > (ratelimit_burst * ratelimit_ms))
        toks = ratelimit_burst * ratelimit_ms;
    if ( toks >= ratelimit_ms )
    {
        int lost = missed;
        missed = 0;
        toks -= ratelimit_ms;
        spin_unlock(&ratelimit_lock);
        if ( lost )
        {
            char lost_str[8];
            snprintf(lost_str, sizeof(lost_str), "%d", lost);
            /* console_lock may already be acquired by printk(). */
            rspin_lock(&console_lock);
            printk_start_of_line("(PRTOS) ");
            __putstr("printk: ");
            __putstr(lost_str);
            __putstr(" messages suppressed.\n");
            rspin_unlock(&console_lock);
        }
        local_irq_restore(flags);
        return 1;
    }
    missed++;
    spin_unlock_irqrestore(&ratelimit_lock, flags);
    return 0;
}

/* minimum time in ms between messages */
static int __read_mostly printk_ratelimit_ms = 5 * 1000;

/* number of messages we send before ratelimiting */
static int __read_mostly printk_ratelimit_burst = 10;

int printk_ratelimit(void)
{
    return __printk_ratelimit(printk_ratelimit_ms, printk_ratelimit_burst);
}

/*
 * **************************************************************
 * ********************** Error-report **************************
 * **************************************************************
 */

void panic(const char *fmt, ...)
{
    va_list args;
    unsigned long flags;
    static DEFINE_SPINLOCK(lock);
    static char buf[128];

    spin_debug_disable();
    spinlock_profile_printall('\0');
    debugtrace_dump();

    /* Protects buf[] and ensure multi-line message prints atomically. */
    spin_lock_irqsave(&lock, flags);

    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    console_start_sync();
    printk("\n****************************************\n");
    printk("Panic on CPU %d:\n", smp_processor_id());
    printk("%s", buf);
    printk("****************************************\n\n");
    if ( opt_noreboot )
        printk("Manual reset required ('noreboot' specified)\n");
    else
#ifdef CONFIG_X86
        printk("%s in five seconds...\n", pv_shim ? "Crash" : "Reboot");
#else
        printk("Reboot in five seconds...\n");
#endif

    spin_unlock_irqrestore(&lock, flags);

    kexec_crash(CRASHREASON_PANIC);

    if ( opt_noreboot )
        machine_halt();
    else
        machine_restart(5000);
}

/*
 * **************************************************************
 * ****************** Console suspend/resume ********************
 * **************************************************************
 */



/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */


/* === END INLINED: console.c === */
/* === BEGIN INLINED: arm-uart.c === */
#include <prtos_prtos_config.h>
/*
 * prtos/drivers/char/arm-uart.c
 *
 * Generic uart retrieved via the device tree or ACPI
 *
 * Julien Grall <julien.grall@linaro.org>
 * Copyright (c) 2013 Linaro Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm_generic_device.h>

#include <prtos_console.h>
#include <prtos_device_tree.h>
#include <prtos_param.h>
#include <prtos_serial.h>
#include <prtos_errno.h>
#include <prtos_acpi.h>

/*
 * Configure UART port with a string:
 * path:options
 *
 * @path: full path used in the device tree for the UART. If the path
 * doesn't start with '/', we assuming that it's an alias.
 * @options: UART speficic options (see in each UART driver)
 */
static char __initdata opt_dtuart[256] = "";
string_param("dtuart", opt_dtuart);

static void __init dt_uart_init(void)
{
    struct dt_device_node *dev;
    int ret;
    const char *devpath = opt_dtuart;
    const char *options;
    char *split;

    if ( !console_has("dtuart") )
        return; /* Not for us */

    if ( !strcmp(opt_dtuart, "") )
    {
        const struct dt_device_node *chosen = dt_find_node_by_path("/chosen");

        if ( chosen )
        {
            const char *stdout;

            ret = dt_property_read_string(chosen, "stdout-path", &stdout);
            if ( ret >= 0 )
            {
                printk("Taking dtuart configuration from /chosen/stdout-path\n");
                if ( strlcpy(opt_dtuart, stdout, sizeof(opt_dtuart))
                     >= sizeof(opt_dtuart) )
                    printk("WARNING: /chosen/stdout-path too long, truncated\n");
            }
            else if ( ret != -EINVAL /* Not present */ )
                printk("Failed to read /chosen/stdout-path (%d)\n", ret);
        }
    }

    if ( !strcmp(opt_dtuart, "") )
    {
        printk("No dtuart path configured\n");
        return;
    }

    split = strchr(opt_dtuart, ':');
    if ( split )
    {
        split[0] = '\0';
        options = split + 1;
    }
    else
        options = "";

    printk("Looking for dtuart at \"%s\", options \"%s\"\n", devpath, options);
    if ( *devpath == '/' )
        dev = dt_find_node_by_path(devpath);
    else
        dev = dt_find_node_by_alias(devpath);

    if ( !dev )
    {
        printk("Unable to find device \"%s\"\n", devpath);
        return;
    }

    ret = device_init(dev, DEVICE_SERIAL, options);

    if ( ret )
        printk("Unable to initialize dtuart: %d\n", ret);
}

#ifdef CONFIG_ACPI
static void __init acpi_uart_init(void)
{
    struct acpi_table_spcr *spcr = NULL;
    int ret;

    acpi_get_table(ACPI_SIG_SPCR, 0, (struct acpi_table_header **)&spcr);

    if ( spcr == NULL )
    {
        printk("Unable to get spcr table\n");
    }
    else
    {
        ret = acpi_device_init(DEVICE_SERIAL, NULL, spcr->interface_type);

        if ( ret )
            printk("Unable to initialize acpi uart: %d\n", ret);
    }
}
#else
static void __init acpi_uart_init(void) { }
#endif

void __init arm_uart_init(void)
{
    if ( acpi_disabled )
        dt_uart_init();
    else
        acpi_uart_init();
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arm-uart.c === */
/* vpl011.c compiled separately - PRTOS_WANT_FLEX_CONSOLE_RING conflicts with console.h include guard */
/* === BEGIN INLINED: vuart.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/vuart.c
 *
 * Virtual UART Emulator.
 *
 * This emulator uses the information from dtuart. This is not intended to be
 * a full emulation of an UART device. Rather it is intended to provide a
 * sufficient veneer of one that early code (such as Linux's boot time
 * decompressor) which hardcodes output directly to such a device are able to
 * make progress.
 *
 * The minimal register set to emulate an UART are:
 *  - Single byte transmit register
 *  - Single status register
 *
 * /!\ This device is not intended to be enumerable or exposed to the OS
 * (e.g. via Device Tree).
 *
 * Julien Grall <julien.grall@linaro.org>
 * Ian Campbell <ian.campbell@citrix.com>
 * Copyright (c) 2012 Citrix Systems.
 */
#include <prtos_lib.h>
#include <prtos_sched.h>
#include <prtos_errno.h>
#include <prtos_ctype.h>
#include <prtos_serial.h>
#include <asm_mmio.h>
#include <prtos_perfc.h>

#include "vuart.h"

#define domain_has_vuart(d) ((d)->arch.vuart.info != NULL)

static int vuart_mmio_read(struct vcpu *v, mmio_info_t *info,
                           register_t *r, void *priv);
static int vuart_mmio_write(struct vcpu *v, mmio_info_t *info,
                            register_t r, void *priv);

static const struct mmio_handler_ops vuart_mmio_handler = {
    .read  = vuart_mmio_read,
    .write = vuart_mmio_write,
};

int domain_vuart_init(struct domain *d)
{
    d->arch.vuart.info = serial_vuart_info(SERHND_DTUART);
    if ( !d->arch.vuart.info )
        return 0;

    spin_lock_init(&d->arch.vuart.lock);
    d->arch.vuart.idx = 0;

    d->arch.vuart.buf = xzalloc_array(char, VUART_BUF_SIZE);
    if ( !d->arch.vuart.buf )
        return -ENOMEM;

    register_mmio_handler(d, &vuart_mmio_handler,
                          d->arch.vuart.info->base_addr,
                          d->arch.vuart.info->size,
                          NULL);

    return 0;
}

void domain_vuart_free(struct domain *d)
{
    if ( !domain_has_vuart(d) )
        return;

    xfree(d->arch.vuart.buf);
}

static void vuart_print_char(struct vcpu *v, char c)
{
    struct domain *d = v->domain;
    struct vuart *uart = &d->arch.vuart;

    /* Accept only printable characters, newline, and horizontal tab. */
    if ( !isprint(c) && (c != '\n') && (c != '\t') )
        return ;

    spin_lock(&uart->lock);
    uart->buf[uart->idx++] = c;
    if ( (uart->idx == (VUART_BUF_SIZE - 2)) || (c == '\n') )
    {
        if ( c != '\n' )
            uart->buf[uart->idx++] = '\n';
        uart->buf[uart->idx] = '\0';
        printk(PRTOSLOG_G_DEBUG "DOM%u: %s", d->domain_id, uart->buf);
        uart->idx = 0;
    }
    spin_unlock(&uart->lock);
}

static int vuart_mmio_read(struct vcpu *v, mmio_info_t *info,
                           register_t *r, void *priv)
{
    struct domain *d = v->domain;
    paddr_t offset = info->gpa - d->arch.vuart.info->base_addr;

    perfc_incr(vuart_reads);

    /* By default zeroed the register */
    *r = 0;

    if ( offset == d->arch.vuart.info->status_off )
        /* All holding registers empty, ready to send etc */
        *r = d->arch.vuart.info->status;

    return 1;
}

static int vuart_mmio_write(struct vcpu *v, mmio_info_t *info,
                            register_t r, void *priv)
{
    struct domain *d = v->domain;
    paddr_t offset = info->gpa - d->arch.vuart.info->base_addr;

    perfc_incr(vuart_writes);

    if ( offset == d->arch.vuart.info->data_off )
        /* ignore any status bits */
        vuart_print_char(v, r & 0xFF);

    return 1;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */


/* === END INLINED: vuart.c === */
/* === BEGIN INLINED: early_printk.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * printk() for use before the final page tables are setup.
 *
 * Copyright (C) 2012 Citrix Systems, Inc.
 */

#include <prtos_prtos_config.h>

#include <prtos_init.h>
#include <prtos_lib.h>
#include <prtos_stdarg.h>
#include <prtos_string.h>
#include <prtos_early_printk.h>

void early_putch(char c);
void early_flush(void);

void early_puts(const char *s, size_t nr)
{
    while ( nr-- > 0 )
    {
        if (*s == '\n')
            early_putch('\r');
        early_putch(*s);
        s++;
    }

    /*
     * Wait the UART has finished to transfer all characters before
     * to continue. This will avoid lost characters if PRTOS abort.
     */
    early_flush();
}

/* === END INLINED: early_printk.c === */
/* === BEGIN INLINED: platform.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/platform.c
 *
 * Helpers to execute platform specific code.
 *
 * Julien Grall <julien.grall@linaro.org>
 * Copyright (C) 2013 Linaro Limited.
 */

#include <asm_platform.h>
#include <prtos_device_tree.h>
#include <prtos_init.h>
#include <asm_psci.h>

extern const struct platform_desc _splatform[], _eplatform[];

/* Pointer to the current platform description */
static const struct platform_desc *platform;


static bool __init platform_is_compatible(const struct platform_desc *plat)
{
    const char *const *compat;

    if ( !plat->compatible )
        return false;

    for ( compat = plat->compatible; *compat; compat++ )
    {
        if ( dt_machine_is_compatible(*compat) )
            return true;
    }

    return false;
}

void __init platform_init(void)
{
    int res = 0;

    ASSERT(platform == NULL);

    /* Looking for the platform description */
    for ( platform = _splatform; platform != _eplatform; platform++ )
    {
        if ( platform_is_compatible(platform) )
            break;
    }

    /* We don't have specific operations for this platform */
    if ( platform == _eplatform )
    {
        /* TODO: dump DT machine compatible node */
        printk(PRTOSLOG_INFO "Platform: Generic System\n");
        platform = NULL;
    }
    else
        printk(PRTOSLOG_INFO "Platform: %s\n", platform->name);

    if ( platform && platform->init )
        res = platform->init();

    if ( res )
        panic("Unable to initialize the platform\n");
}

int __init platform_init_time(void)
{
    int res = 0;

    if ( platform && platform->init_time )
        res = platform->init_time();

    return res;
}

int __init platform_specific_mapping(struct domain *d)
{
    int res = 0;

    if ( platform && platform->specific_mapping )
        res = platform->specific_mapping(d);

    return res;
}

#ifdef CONFIG_ARM_32
int platform_cpu_up(int cpu)
{
    if ( psci_ver )
        return call_psci_cpu_on(cpu);

    if ( platform && platform->cpu_up )
        return platform->cpu_up(cpu);

    return -ENODEV;
}

int __init platform_smp_init(void)
{
    if ( platform && platform->smp_init )
        return platform->smp_init();

    return 0;
}
#endif

void platform_reset(void)
{
    if ( platform && platform->reset )
        platform->reset();
}

void platform_poweroff(void)
{
    if ( platform && platform->poweroff )
        platform->poweroff();
}

bool platform_smc(struct cpu_user_regs *regs)
{
    if ( likely(platform && platform->smc) )
        return platform->smc(regs);

    return false;
}


bool platform_device_is_blacklisted(const struct dt_device_node *node)
{
    const struct dt_device_match *blacklist = NULL;

    if ( platform && platform->blacklist_dev )
        blacklist = platform->blacklist_dev;

    return (dt_match_node(blacklist, node) != NULL);
}

unsigned int arch_get_dma_bitsize(void)
{
    return ( platform && platform->dma_bitsize ) ? platform->dma_bitsize : 32;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: platform.c === */
/* === BEGIN INLINED: iommu.c === */
#include <prtos_prtos_config.h>
/*
 * Generic IOMMU framework via the device tree
 *
 * Julien Grall <julien.grall@linaro.org>
 * Copyright (c) 2014 Linaro Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <prtos_device_tree.h>
#include <prtos_iommu.h>
#include <prtos_lib.h>

#include <asm_generic_device.h>

/*
 * Deferred probe list is used to keep track of devices for which driver
 * requested deferred probing (returned -EAGAIN).
 */
static __initdata LIST_HEAD(deferred_probe_list);

static const struct iommu_ops *iommu_ops;

const struct iommu_ops *iommu_get_ops(void)
{
    return iommu_ops;
}


int __init iommu_hardware_setup(void)
{
    struct dt_device_node *np, *tmp;
    int rc;
    unsigned int num_iommus = 0;

    dt_for_each_device_node(dt_host, np)
    {
        rc = device_init(np, DEVICE_IOMMU, NULL);
        if ( !rc )
            num_iommus++;
        else if ( rc == -EAGAIN )
        {
            /*
             * Nobody should use device's domain_list at such early stage,
             * so we can re-use it to link the device in the deferred list to
             * avoid introducing extra list_head field in struct dt_device_node.
             */
            ASSERT(list_empty(&np->domain_list));

            /*
             * Driver requested deferred probing, so add this device to
             * the deferred list for further processing.
             */
            list_add(&np->domain_list, &deferred_probe_list);
        }
        /*
         * Ignore the following error codes:
         *   - EBADF: Indicate the current is not an IOMMU
         *   - ENODEV: The IOMMU is not present or cannot be used by
         *     PRTOS.
         */
        else if ( rc != -EBADF && rc != -ENODEV )
            return rc;
    }

    /* Return immediately if there are no initialized devices. */
    if ( !num_iommus )
        return list_empty(&deferred_probe_list) ? -ENODEV : -EAGAIN;

    rc = 0;

    /*
     * Process devices in the deferred list if it is not empty.
     * Check that at least one device is initialized at each loop, otherwise
     * we may get an infinite loop. Also stop processing if we got an error
     * other than -EAGAIN.
     */
    while ( !list_empty(&deferred_probe_list) && num_iommus )
    {
        num_iommus = 0;

        list_for_each_entry_safe ( np, tmp, &deferred_probe_list, domain_list )
        {
            rc = device_init(np, DEVICE_IOMMU, NULL);
            if ( !rc )
            {
                num_iommus++;

                /* Remove initialized device from the deferred list. */
                list_del_init(&np->domain_list);
            }
            else if ( rc != -EAGAIN )
                return rc;
        }
    }

    return rc;
}

void __hwdom_init arch_iommu_check_autotranslated_hwdom(struct domain *d)
{
    /* ARM doesn't require specific check for hwdom */
    return;
}

int arch_iommu_domain_init(struct domain *d)
{
    return iommu_dt_domain_init(d);
}

void arch_iommu_domain_destroy(struct domain *d)
{
}



/* === END INLINED: iommu.c === */
/* === BEGIN INLINED: iommu_fwspec.c === */
#include <prtos_prtos_config.h>
/*
 * prtos/drivers/passthrough/arm/iommu_fwspec.c
 *
 * Contains functions to maintain per-device firmware data
 *
 * Based on Linux's iommu_fwspec support you can find at:
 *    drivers/iommu/iommu.c
 *
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 *
 * Copyright (C) 2019 EPAM Systems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms and conditions of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_iommu.h>
#include <prtos_lib.h>

#include <asm_generic_device.h>
#include <asm_iommu_fwspec.h>

int iommu_fwspec_init(struct device *dev, struct device *iommu_dev)
{
    struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

    if ( fwspec )
    {
        /* We expect the device to be protected by only one IOMMU. */
        if ( fwspec->iommu_dev != iommu_dev )
            return -EINVAL;

        return 0;
    }

    /*
     * Allocate with ids[1] to avoid the re-allocation in the common case
     * where a device has a single device ID.
     */
    fwspec = xzalloc_flex_struct(struct iommu_fwspec, ids, 1);
    if ( !fwspec )
        return -ENOMEM;

    fwspec->iommu_dev = iommu_dev;
    dev_iommu_fwspec_set(dev, fwspec);

    return 0;
}

void iommu_fwspec_free(struct device *dev)
{
    struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

    xfree(fwspec);
    dev_iommu_fwspec_set(dev, NULL);
}


/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: iommu_fwspec.c === */
/* === BEGIN INLINED: iommu_helpers.c === */
#include <prtos_prtos_config.h>
/*
 * prtos/drivers/passthrough/arm/iommu_helpers.c
 *
 * Contains various helpers to be used by IOMMU drivers.
 *
 * Based on PRTOS's SMMU driver:
 *    prtos/drivers/passthrough/arm/smmu.c
 *
 * Copyright (C) 2014 Linaro Limited.
 *
 * Copyright (C) 2019 EPAM Systems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms and conditions of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_iommu.h>
#include <prtos_lib.h>
#include <prtos_sched.h>

#include <asm_generic_device.h>



/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: iommu_helpers.c === */
/* === BEGIN INLINED: drivers_passthrough_iommu.c === */
#include <prtos_prtos_config.h>
/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_sched.h>
#include <prtos_iommu.h>
#include <prtos_paging.h>
#include <prtos_guest_access.h>
#include <prtos_event.h>
#include <prtos_param.h>
#include <prtos_softirq.h>
#include <prtos_keyhandler.h>
#include <prtos_xsm_xsm.h>

#ifdef CONFIG_X86
#include <asm/e820.h>
#endif

unsigned int __read_mostly iommu_dev_iotlb_timeout = 1000;
integer_param("iommu_dev_iotlb_timeout", iommu_dev_iotlb_timeout);

bool __initdata iommu_enable = 1;
bool __read_mostly iommu_enabled;
bool __read_mostly force_iommu;
bool __read_mostly iommu_verbose;
static bool __read_mostly iommu_crash_disable;

#define IOMMU_quarantine_none         0 /* aka false */
#define IOMMU_quarantine_basic        1 /* aka true */
#define IOMMU_quarantine_scratch_page 2
#ifdef CONFIG_HAS_PCI
uint8_t __read_mostly iommu_quarantine =
# if defined(CONFIG_IOMMU_QUARANTINE_NONE)
    IOMMU_quarantine_none;
# elif defined(CONFIG_IOMMU_QUARANTINE_BASIC)
    IOMMU_quarantine_basic;
# elif defined(CONFIG_IOMMU_QUARANTINE_SCRATCH_PAGE)
    IOMMU_quarantine_scratch_page;
# endif
#else
# define iommu_quarantine IOMMU_quarantine_none
#endif /* CONFIG_HAS_PCI */

static bool __hwdom_initdata iommu_hwdom_none;
bool __hwdom_initdata iommu_hwdom_strict;
bool __read_mostly iommu_hwdom_passthrough;
bool __hwdom_initdata iommu_hwdom_inclusive;
int8_t __hwdom_initdata iommu_hwdom_reserved = -1;

#ifndef iommu_hap_pt_share
bool __read_mostly iommu_hap_pt_share = true;
#endif

bool __read_mostly iommu_debug;

DEFINE_PER_CPU(bool, iommu_dont_flush_iotlb);

static int __init cf_check parse_iommu_param(const char *s)
{
    const char *ss;
    int val, rc = 0;

    do {
        ss = strchr(s, ',');
        if ( !ss )
            ss = strchr(s, '\0');

        if ( (val = parse_bool(s, ss)) >= 0 )
            iommu_enable = val;
        else if ( (val = parse_boolean("force", s, ss)) >= 0 ||
                  (val = parse_boolean("required", s, ss)) >= 0 )
            force_iommu = val;
#ifdef CONFIG_HAS_PCI
        else if ( (val = parse_boolean("quarantine", s, ss)) >= 0 )
            iommu_quarantine = val;
        else if ( ss == s + 23 && !strncmp(s, "quarantine=scratch-page", 23) )
            iommu_quarantine = IOMMU_quarantine_scratch_page;
#endif
        else if ( (val = parse_boolean("igfx", s, ss)) >= 0 )
#ifdef CONFIG_INTEL_IOMMU
            iommu_igfx = val;
#else
            no_config_param("INTEL_IOMMU", "iommu", s, ss);
#endif
        else if ( (val = parse_boolean("qinval", s, ss)) >= 0 )
#ifdef CONFIG_INTEL_IOMMU
            iommu_qinval = val;
#else
            no_config_param("INTEL_IOMMU", "iommu", s, ss);
#endif
#ifdef CONFIG_X86
        else if ( (val = parse_boolean("superpages", s, ss)) >= 0 )
            iommu_superpages = val;
#endif
        else if ( (val = parse_boolean("verbose", s, ss)) >= 0 )
            iommu_verbose = val;
#ifndef iommu_snoop
        else if ( (val = parse_boolean("snoop", s, ss)) >= 0 )
            iommu_snoop = val;
#endif
#ifndef iommu_intremap
        else if ( (val = parse_boolean("intremap", s, ss)) >= 0 )
            iommu_intremap = val ? iommu_intremap_full : iommu_intremap_off;
#endif
#ifndef iommu_intpost
        else if ( (val = parse_boolean("intpost", s, ss)) >= 0 )
            iommu_intpost = val;
#endif
#ifdef CONFIG_KEXEC
        else if ( (val = parse_boolean("crash-disable", s, ss)) >= 0 )
            iommu_crash_disable = val;
#endif
        else if ( (val = parse_boolean("debug", s, ss)) >= 0 )
        {
            iommu_debug = val;
            if ( val )
                iommu_verbose = 1;
        }
        else if ( (val = parse_boolean("amd-iommu-perdev-intremap", s, ss)) >= 0 )
#ifdef CONFIG_AMD_IOMMU
            amd_iommu_perdev_intremap = val;
#else
            no_config_param("AMD_IOMMU", "iommu", s, ss);
#endif
        else if ( (val = parse_boolean("dom0-passthrough", s, ss)) >= 0 )
            iommu_hwdom_passthrough = val;
        else if ( (val = parse_boolean("dom0-strict", s, ss)) >= 0 )
            iommu_hwdom_strict = val;
#ifndef iommu_hap_pt_share
        else if ( (val = parse_boolean("sharept", s, ss)) >= 0 )
            iommu_hap_pt_share = val;
#endif
        else
            rc = -EINVAL;

        s = ss + 1;
    } while ( *ss );

    return rc;
}
custom_param("iommu", parse_iommu_param);

static int __init cf_check parse_dom0_iommu_param(const char *s)
{
    const char *ss;
    int rc = 0;

    do {
        int val;

        ss = strchr(s, ',');
        if ( !ss )
            ss = strchr(s, '\0');

        if ( (val = parse_boolean("passthrough", s, ss)) >= 0 )
            iommu_hwdom_passthrough = val;
        else if ( (val = parse_boolean("strict", s, ss)) >= 0 )
            iommu_hwdom_strict = val;
        else if ( (val = parse_boolean("map-inclusive", s, ss)) >= 0 )
            iommu_hwdom_inclusive = val;
        else if ( (val = parse_boolean("map-reserved", s, ss)) >= 0 )
            iommu_hwdom_reserved = val;
        else if ( !cmdline_strcmp(s, "none") )
            iommu_hwdom_none = true;
        else
            rc = -EINVAL;

        s = ss + 1;
    } while ( *ss );

    return rc;
}
custom_param("dom0-iommu", parse_dom0_iommu_param);

static void __hwdom_init check_hwdom_reqs(struct domain *d)
{
    if ( iommu_hwdom_none || !is_hvm_domain(d) )
        return;

    iommu_hwdom_passthrough = false;
    iommu_hwdom_strict = true;

    arch_iommu_check_autotranslated_hwdom(d);
}

int iommu_domain_init(struct domain *d, unsigned int opts)
{
    struct domain_iommu *hd = dom_iommu(d);
    int ret = 0;

    if ( is_hardware_domain(d) )
        check_hwdom_reqs(d); /* may modify iommu_hwdom_strict */

    if ( !is_iommu_enabled(d) )
        return 0;

#ifdef CONFIG_NUMA
    hd->node = NUMA_NO_NODE;
#endif

    ret = arch_iommu_domain_init(d);
    if ( ret )
        return ret;

    hd->platform_ops = iommu_get_ops();
    ret = iommu_call(hd->platform_ops, init, d);
    if ( ret || is_system_domain(d) )
        return ret;

    /*
     * Use shared page tables for HAP and IOMMU if the global option
     * is enabled (from which we can infer the h/w is capable) and
     * the domain options do not disallow it. HAP must, of course, also
     * be enabled.
     */
    hd->hap_pt_share = hap_enabled(d) && iommu_hap_pt_share &&
        !(opts & PRTOS_DOMCTL_IOMMU_no_sharept);

    /*
     * NB: 'relaxed' h/w domains don't need the IOMMU mappings to be kept
     *     in-sync with their assigned pages because all host RAM will be
     *     mapped during hwdom_init().
     */
    if ( !is_hardware_domain(d) || iommu_hwdom_strict )
        hd->need_sync = !iommu_use_hap_pt(d);

    ASSERT(!(hd->need_sync && hd->hap_pt_share));

    return 0;
}

static void cf_check iommu_dump_page_tables(unsigned char key)
{
    struct domain *d;

    ASSERT(iommu_enabled);

    rcu_read_lock(&domlist_read_lock);

    for_each_domain(d)
    {
        if ( is_hardware_domain(d) || !is_iommu_enabled(d) )
            continue;

        if ( iommu_use_hap_pt(d) )
        {
            printk("%pd sharing page tables\n", d);
            continue;
        }

        iommu_vcall(dom_iommu(d)->platform_ops, dump_page_tables, d);
    }

    rcu_read_unlock(&domlist_read_lock);
}

void __hwdom_init iommu_hwdom_init(struct domain *d)
{
    struct domain_iommu *hd = dom_iommu(d);

    if ( !is_iommu_enabled(d) )
        return;

    register_keyhandler('o', &iommu_dump_page_tables, "dump iommu page tables", 0);

    iommu_vcall(hd->platform_ops, hwdom_init, d);
}

static void iommu_teardown(struct domain *d)
{
    struct domain_iommu *hd = dom_iommu(d);

    /*
     * During early domain creation failure, we may reach here with the
     * ops not yet initialized.
     */
    if ( !hd->platform_ops )
        return;

    iommu_vcall(hd->platform_ops, teardown, d);
}

void iommu_domain_destroy(struct domain *d)
{
    if ( !is_iommu_enabled(d) )
        return;

    iommu_teardown(d);

    arch_iommu_domain_destroy(d);
}

static unsigned int mapping_order(const struct domain_iommu *hd,
                                  dfn_t dfn, mfn_t mfn, unsigned long nr)
{
    unsigned long res = dfn_x(dfn) | mfn_x(mfn);
    unsigned long sizes = hd->platform_ops->page_sizes;
    unsigned int bit = ffsl(sizes) - 1, order = 0;

    ASSERT(bit == PAGE_SHIFT);

    while ( (sizes = (sizes >> bit) & ~1) )
    {
        unsigned long mask;

        bit = ffsl(sizes) - 1;
        mask = (1UL << bit) - 1;
        if ( nr <= mask || (res & mask) )
            break;
        order += bit;
        nr >>= bit;
        res >>= bit;
    }

    return order;
}

long iommu_map(struct domain *d, dfn_t dfn0, mfn_t mfn0,
               unsigned long page_count, unsigned int flags,
               unsigned int *flush_flags)
{
    const struct domain_iommu *hd = dom_iommu(d);
    unsigned long i;
    unsigned int order, j = 0;
    int rc = 0;

    if ( !is_iommu_enabled(d) )
        return 0;

    ASSERT(!IOMMUF_order(flags));

    for ( i = 0; i < page_count; i += 1UL << order )
    {
        dfn_t dfn = dfn_add(dfn0, i);
        mfn_t mfn = mfn_add(mfn0, i);

        order = mapping_order(hd, dfn, mfn, page_count - i);

        if ( (flags & IOMMUF_preempt) &&
             ((!(++j & 0xfff) && general_preempt_check()) ||
              i > LONG_MAX - (1UL << order)) )
            return i;

        rc = iommu_call(hd->platform_ops, map_page, d, dfn, mfn,
                        flags | IOMMUF_order(order), flush_flags);

        if ( likely(!rc) )
            continue;

        if ( !d->is_shutting_down && printk_ratelimit() )
            printk(PRTOSLOG_ERR
                   "d%d: IOMMU mapping dfn %"PRI_dfn" to mfn %"PRI_mfn" failed: %d\n",
                   d->domain_id, dfn_x(dfn), mfn_x(mfn), rc);

        /* while statement to satisfy __must_check */
        while ( iommu_unmap(d, dfn0, i, 0, flush_flags) )
            break;

        if ( !is_hardware_domain(d) )
            domain_crash(d);

        break;
    }

    /*
     * Something went wrong so, if we were dealing with more than a single
     * page, flush everything and clear flush flags.
     */
    if ( page_count > 1 && unlikely(rc) &&
         !iommu_iotlb_flush_all(d, *flush_flags) )
        *flush_flags = 0;

    return rc;
}

int iommu_legacy_map(struct domain *d, dfn_t dfn, mfn_t mfn,
                     unsigned long page_count, unsigned int flags)
{
    unsigned int flush_flags = 0;
    int rc;

    ASSERT(!(flags & IOMMUF_preempt));
    rc = iommu_map(d, dfn, mfn, page_count, flags, &flush_flags);

    if ( !this_cpu(iommu_dont_flush_iotlb) && !rc )
        rc = iommu_iotlb_flush(d, dfn, page_count, flush_flags);

    return rc;
}

long iommu_unmap(struct domain *d, dfn_t dfn0, unsigned long page_count,
                 unsigned int flags, unsigned int *flush_flags)
{
    const struct domain_iommu *hd = dom_iommu(d);
    unsigned long i;
    unsigned int order, j = 0;
    int rc = 0;

    if ( !is_iommu_enabled(d) )
        return 0;

    ASSERT(!(flags & ~IOMMUF_preempt));

    for ( i = 0; i < page_count; i += 1UL << order )
    {
        dfn_t dfn = dfn_add(dfn0, i);
        int err;

        order = mapping_order(hd, dfn, _mfn(0), page_count - i);

        if ( (flags & IOMMUF_preempt) &&
             ((!(++j & 0xfff) && general_preempt_check()) ||
              i > LONG_MAX - (1UL << order)) )
            return i;

        err = iommu_call(hd->platform_ops, unmap_page, d, dfn,
                         flags | IOMMUF_order(order), flush_flags);

        if ( likely(!err) )
            continue;

        if ( !d->is_shutting_down && printk_ratelimit() )
            printk(PRTOSLOG_ERR
                   "d%d: IOMMU unmapping dfn %"PRI_dfn" failed: %d\n",
                   d->domain_id, dfn_x(dfn), err);

        if ( !rc )
            rc = err;

        if ( !is_hardware_domain(d) )
        {
            domain_crash(d);
            break;
        }
    }

    /*
     * Something went wrong so, if we were dealing with more than a single
     * page, flush everything and clear flush flags.
     */
    if ( page_count > 1 && unlikely(rc) &&
         !iommu_iotlb_flush_all(d, *flush_flags) )
        *flush_flags = 0;

    return rc;
}

int iommu_legacy_unmap(struct domain *d, dfn_t dfn, unsigned long page_count)
{
    unsigned int flush_flags = 0;
    int rc = iommu_unmap(d, dfn, page_count, 0, &flush_flags);

    if ( !this_cpu(iommu_dont_flush_iotlb) && !rc )
        rc = iommu_iotlb_flush(d, dfn, page_count, flush_flags);

    return rc;
}


int iommu_iotlb_flush(struct domain *d, dfn_t dfn, unsigned long page_count,
                      unsigned int flush_flags)
{
    const struct domain_iommu *hd = dom_iommu(d);
    int rc;

    if ( !is_iommu_enabled(d) || !hd->platform_ops->iotlb_flush ||
         !page_count || !flush_flags )
        return 0;

    if ( dfn_eq(dfn, INVALID_DFN) )
        return -EINVAL;

    rc = iommu_call(hd->platform_ops, iotlb_flush, d, dfn, page_count,
                    flush_flags);
    if ( unlikely(rc) )
    {
        if ( !d->is_shutting_down && printk_ratelimit() )
            printk(PRTOSLOG_ERR
                   "d%d: IOMMU IOTLB flush failed: %d, dfn %"PRI_dfn", page count %lu flags %x\n",
                   d->domain_id, rc, dfn_x(dfn), page_count, flush_flags);

        if ( !is_hardware_domain(d) )
            domain_crash(d);
    }

    return rc;
}

int iommu_iotlb_flush_all(struct domain *d, unsigned int flush_flags)
{
    const struct domain_iommu *hd = dom_iommu(d);
    int rc;

    if ( !is_iommu_enabled(d) || !hd->platform_ops->iotlb_flush ||
         !flush_flags )
        return 0;

    rc = iommu_call(hd->platform_ops, iotlb_flush, d, INVALID_DFN, 0,
                    flush_flags | IOMMU_FLUSHF_all);
    if ( unlikely(rc) )
    {
        if ( !d->is_shutting_down && printk_ratelimit() )
            printk(PRTOSLOG_ERR
                   "d%d: IOMMU IOTLB flush all failed: %d\n",
                   d->domain_id, rc);

        if ( !is_hardware_domain(d) )
            domain_crash(d);
    }

    return rc;
}


static int __init iommu_quarantine_init(void)
{
    dom_io->options |= PRTOS_DOMCTL_CDF_iommu;

    return iommu_domain_init(dom_io, 0);
}

int __init iommu_setup(void)
{
    int rc = -ENODEV;
    bool force_intremap = force_iommu && iommu_intremap;

    if ( iommu_hwdom_strict )
        iommu_hwdom_passthrough = false;

    if ( iommu_enable )
    {
        const struct iommu_ops *ops = NULL;

        rc = iommu_hardware_setup();
        if ( !rc )
            ops = iommu_get_ops();
        if ( ops && (ISOLATE_LSB(ops->page_sizes)) != PAGE_SIZE )
        {
            printk(PRTOSLOG_ERR "IOMMU: page size mask %lx unsupported\n",
                   ops->page_sizes);
            rc = ops->page_sizes ? -EPERM : -ENODATA;
        }
        iommu_enabled = (rc == 0);
    }

#ifndef iommu_intremap
    if ( !iommu_enabled )
        iommu_intremap = iommu_intremap_off;
#endif

    if ( (force_iommu && !iommu_enabled) ||
         (force_intremap && !iommu_intremap) )
        panic("Couldn't enable %s and iommu=required/force\n",
              !iommu_enabled ? "IOMMU" : "Interrupt Remapping");

#ifndef iommu_intpost
    if ( !iommu_intremap )
        iommu_intpost = false;
#endif

    printk("I/O virtualisation %sabled\n", iommu_enabled ? "en" : "dis");
    if ( !iommu_enabled )
    {
        iommu_hwdom_passthrough = false;
        iommu_hwdom_strict = false;
    }
    else
    {
        if ( iommu_quarantine_init() )
            panic("Could not set up quarantine\n");

        printk(" - Dom0 mode: %s\n",
               iommu_hwdom_passthrough ? "Passthrough" :
               iommu_hwdom_strict ? "Strict" : "Relaxed");
#ifndef iommu_intremap
        printk("Interrupt remapping %sabled\n", iommu_intremap ? "en" : "dis");
#endif
    }

    return rc;
}



int iommu_do_domctl(
    struct prtos_domctl *domctl, struct domain *d,
    PRTOS_GUEST_HANDLE_PARAM(prtos_domctl_t) u_domctl)
{
    int ret = -ENODEV;

    if ( !(d ? is_iommu_enabled(d) : iommu_enabled) )
        return -EOPNOTSUPP;

#ifdef CONFIG_HAS_PCI
    ret = iommu_do_pci_domctl(domctl, d, u_domctl);
#endif

#ifdef CONFIG_HAS_DEVICE_TREE
    if ( ret == -ENODEV )
        ret = iommu_do_dt_domctl(domctl, d, u_domctl);
#endif

    return ret;
}


int iommu_get_reserved_device_memory(iommu_grdm_t *func, void *ctxt)
{
    const struct iommu_ops *ops;

    if ( !iommu_enabled )
        return 0;

    ops = iommu_get_ops();
    if ( !ops->get_reserved_device_memory )
        return 0;

    return iommu_call(ops, get_reserved_device_memory, func, ctxt);
}

bool iommu_has_feature(struct domain *d, enum iommu_feature feature)
{
    return is_iommu_enabled(d) && test_bit(feature, dom_iommu(d)->features);
}



/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: drivers_passthrough_iommu.c === */
