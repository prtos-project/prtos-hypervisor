#if !defined(_BOARD_H)
#define  _BOARD_H

/*
 * FreeRTOS on PRTOS (HW-Virt) - Board definitions
 *
 * GIC: Managed by hypervisor. Guest uses GICv3 system registers
 * (ICC_*_EL1) via the virtual CPU interface. No MMIO access needed.
 */

#define GIC_INT_MAX				(64)
#define GIC_PRIO_MAX			(16)
#define GIC_INTNO_SGI0			(0)
#define GIC_INTNO_PPI0			(16)
#define GIC_INTNO_SPI0			(32)

#define GIC_PRI_SHIFT			(4)
#define GIC_PRI_MASK			(0x0f)

/* Timer */
#define TIMER_IRQ				(27)  /* Virtual timer PPI */

/* UART - PL011 on QEMU virt (mapped via stage-2 identity map) */
#define QEMU_VIRT_UART_BASE		(0x09000000)

#endif  /* _BOARD_H */
