/*
 * tick_config.c - FreeRTOS tick timer using LoongArch64 CSR.TCFG
 *
 * Uses LoongArch64 constant frequency timer (CSR.TCFG/TVAL/TICLR).
 * Timer interrupt is bit 11 in ESTAT (TI).
 */

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

/* CSR numbers */
#define CSR_TCFG    0x41
#define CSR_TVAL    0x42
#define CSR_TICLR   0x44
#define CSR_ECFG    0x4
#define CSR_ESTAT   0x5

/* TCFG fields */
#define TCFG_EN         (1UL << 0)
#define TCFG_PERIODIC   (1UL << 1)

static inline void csr_write_tcfg(unsigned long val)
{
__asm__ __volatile__("csrwr %0, 0x41" :: "r"(val) : "memory");
}

static inline void csr_write_ticlr(unsigned long val)
{
__asm__ __volatile__("csrwr %0, 0x44" :: "r"(val) : "memory");
}

static inline unsigned long csr_read_ecfg(void)
{
unsigned long val;
__asm__ __volatile__("csrrd %0, 0x4" : "=r"(val));
return val;
}

static inline void csr_write_ecfg(unsigned long val)
{
__asm__ __volatile__("csrwr %0, 0x4" :: "r"(val) : "memory");
}

void vConfigureTickInterrupt(void)
{
unsigned long tick_interval = TIMER_FREQ / configTICK_RATE_HZ;

/* Enable timer interrupt (bit 11) in ECFG.LIE */
unsigned long ecfg = csr_read_ecfg();
ecfg |= (1UL << 11);   /* TI enable */
ecfg |= (1UL << 0);    /* SWI0 enable (for portYIELD) */
csr_write_ecfg(ecfg);

/* Clear any pending timer interrupt */
csr_write_ticlr(1);

/* Configure timer: enable, periodic, with interval */
csr_write_tcfg(TCFG_EN | TCFG_PERIODIC | (tick_interval << 2));
}

void vClearTickInterrupt(void)
{
/* Clear the timer interrupt by writing 1 to TICLR */
csr_write_ticlr(1);
}

/*
 * Called from the trap handler when an interrupt occurs.
 */
void vApplicationIRQHandler(unsigned long estat)
{
unsigned long is = estat & 0x1FFF;

if (is & (1UL << 11)) {
/* Timer interrupt - must clear TI before handling */
vClearTickInterrupt();
FreeRTOS_Tick_Handler();
}
}
