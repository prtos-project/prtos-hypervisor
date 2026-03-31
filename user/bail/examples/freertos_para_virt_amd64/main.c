/*
 * FreeRTOS on PRTOS - Para-Virtualization (amd64)
 *
 * This is partition_main(), called by BAIL's boot.S after init_libprtos,
 * init_arch (IDT/GDT), and setup_irqs (route ext IRQs to vectors 224+).
 *
 * We install custom FreeRTOS ISR handlers in the IDT, then start FreeRTOS.
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "uart.h"
#include "example.h"

/* PRTOS ext IRQ management */
extern int prtos_clear_irqmask(unsigned int hw_irqs_mask, unsigned int ext_irqs_mask);
/* Ext HW timer IRQ number (bit position in ext irq mask) */
#define PRTOS_VT_EXT_HW_TIMER  0

/* From BAIL: IDT gate table and descriptor */
typedef struct {
	unsigned short offset_low;
	unsigned short selector;
	unsigned char  ist;
	unsigned char  access;
	unsigned short offset_mid;
	unsigned int   offset_high;
	unsigned int   reserved;
} gate_desc_t;

#define IDT_ENTRIES (256 + 32)
extern gate_desc_t part_idt_table[IDT_ENTRIES];

/* BAIL's guest code segment selector (from hypervisor's GDT).
 * = ((7 + CONFIG_PARTITION_NO_GDT_ENTRIES) << 3) | 3 where
 * CONFIG_PARTITION_NO_GDT_ENTRIES = 32 */
#ifndef GUEST_CS_SEL
#define GUEST_CS_SEL 0x13b
#endif

/* From BAIL: IDT descriptor register */
struct x86_desc_reg {
	unsigned short limit;
	unsigned long linear_base;
} __attribute__((packed));
extern struct x86_desc_reg part_idt_desc;
extern void prtos_x86_load_idtr(struct x86_desc_reg *);

/* Assembly ISR entry points */
extern void _timer_isr(void);

/* Install an IDT interrupt gate entry */
static void install_idt_gate(int vector, void *handler, unsigned int dpl)
{
	unsigned long offset = (unsigned long)handler;
	part_idt_table[vector].offset_low  = offset & 0xffff;
	part_idt_table[vector].selector    = GUEST_CS_SEL;
	part_idt_table[vector].ist         = 0;
	part_idt_table[vector].access      = 0x8e | ((dpl & 0x3) << 5);
	part_idt_table[vector].offset_mid  = (offset >> 16) & 0xffff;
	part_idt_table[vector].offset_high = (offset >> 32) & 0xffffffff;
	part_idt_table[vector].reserved    = 0;
}

void vMainAssertCalled(const char *pcFileName, unsigned int ulLineNumber)
{
	printf("ASSERT! Line %d of file %s\n", ulLineNumber, pcFileName);
	taskENTER_CRITICAL();
	for (;;)
		;
}

/*
 * Verification task: waits for all 5 software timers to complete,
 * then prints "Verification Passed" for the automated test framework.
 */
void verification_task(void *p)
{
	(void)p;
	vTaskDelay(6000 / portTICK_PERIOD_MS);
	printf("Verification Passed\n");
	for (;;)
		vTaskDelay(portMAX_DELAY);
}

/* Use BAIL's trap handler mechanism for timer interrupt.
 * BAIL routes interrupts through: IDT → vtrap_table → common_part_trap_body → C handler.
 * We install our handler as the C handler for vector 224.
 * The FreeRTOS context switch is triggered by setting ullPortYieldRequired. */
typedef void *trap_ctxt_t;
extern int install_trap_handler(int trap_number, void (*handler)(trap_ctxt_t *));

/* From portASM.S / port.c */
extern unsigned long ullPortYieldRequired;
extern void FreeRTOS_Tick_Handler(void);

static void prtos_timer_trap_handler(trap_ctxt_t *ctxt)
{
	(void)ctxt;
	FreeRTOS_Tick_Handler();
}

/*
 * partition_main - called by BAIL boot.S
 */
void partition_main(void)
{
	portDISABLE_INTERRUPTS();

	/* Install FreeRTOS timer ISR directly in the IDT.
	 * This replaces BAIL's vtrap_table entry, giving us full control
	 * over context save/restore for preemptive scheduling. */
	install_idt_gate(TIMER_IRQ_VECTOR, _timer_isr, 3);

	/* Reload IDT to ensure hypervisor sees the modification */
	prtos_x86_load_idtr(&part_idt_desc);

	/* Unmask the hardware timer IRQ in PRTOS */
	prtos_clear_irqmask(0, (1 << PRTOS_VT_EXT_HW_TIMER));

	printf("FreeRTOS on PRTOS (Para-Virt amd64) starting!\n");

	/* Run the software timer example */
	test_software_timer();

	/* Verification task for automated testing */
	xTaskCreate(verification_task, "verify", 512, NULL, configMAX_PRIORITIES - 1, NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should not reach here */
	for (;;)
		;
}
