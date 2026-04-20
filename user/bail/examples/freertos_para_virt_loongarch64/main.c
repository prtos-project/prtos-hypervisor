/*
 * main.c - FreeRTOS on PRTOS - Para-Virtualization (LoongArch 64)
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "uart.h"
#include "example.h"

extern void _prtos_trap_dispatch(void);
extern unsigned long g_pct_addr;

/* PCT arch structure offset for trap_entry on loongarch64.
 * Must match partition_control_table_t layout. */
#define PCT_TRAP_ENTRY_OFFSET 144

static void register_trap_dispatch(void)
{
	unsigned long pct = g_pct_addr;
	if (pct) {
		volatile unsigned long *trap_entry_ptr =
			(volatile unsigned long *)(pct + PCT_TRAP_ENTRY_OFFSET);
		*trap_entry_ptr = (unsigned long)_prtos_trap_dispatch;
	}
}

static void prvSetupHardware(void)
{
	portDISABLE_INTERRUPTS();
	uart_init();
	register_trap_dispatch();
}

void vMainAssertCalled(const char *pcFileName, unsigned int ulLineNumber)
{
	uart_puts("ASSERT!  Line ");
	uart_puthex(ulLineNumber);
	uart_puts(" of file ");
	uart_puts(pcFileName);
	uart_puts("\n");
	taskENTER_CRITICAL();
	for (;;)
		;
}

extern volatile unsigned long g_tick_count;

void verification_task(void *p)
{
	(void)p;
	uart_puts("verification_task: started\n");

	/* Test 1: Simple context switch */
	portYIELD();
	uart_puts("portYIELD OK\n");

	/* Test 2: Check ticks are working */
	unsigned long start = g_tick_count;
	volatile int j;
	for (j = 0; j < 5000000; j++) ;
	unsigned long end = g_tick_count;
	uart_puts("ticks: ");
	uart_puthex(start);
	uart_puts(" -> ");
	uart_puthex(end);
	uart_puts("\n");

	if (end <= start) {
		uart_puts("ERROR: ticks not advancing!\n");
		for(;;) ;
	}

	/* Test 3: vTaskDelay (context switch + timer) */
	uart_puts("calling vTaskDelay...\n");
	vTaskDelay(50);  /* 500ms at 100Hz */
	uart_puts("after vTaskDelay!\n");

	unsigned long ticks = g_tick_count;
	uart_puts("ticks=");
	uart_puthex(ticks);
	uart_puts("\nVerification Passed\n");

	for (;;) {
		for (j = 0; j < 1000000; j++) ;
	}
}

int main(void)
{
	prvSetupHardware();
	uart_puts("FreeRTOS on PRTOS (Para-Virt LoongArch64) starting!\n");
	xTaskCreate(verification_task, "verify", 1024, NULL, configMAX_PRIORITIES - 1, NULL);
	vTaskStartScheduler();
	for (;;)
		;
	return 0;
}
