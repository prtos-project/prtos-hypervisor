/*
 * FreeRTOS on PRTOS - Hardware-Assisted Virtualization
 * Main entry point.
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "gic_v3.h"
#include "uart.h"
#include "example.h"

static void prvSetupHardware(void)
{
	portDISABLE_INTERRUPTS();

	/* Initialize GIC virtual CPU interface (ICC_* system registers) */
	gic_v3_initialize();
}

void vMainAssertCalled(const char *pcFileName, uint32_t ulLineNumber)
{
	uart_puts("ASSERT!  Line ");
	uart_puthex(ulLineNumber);
	uart_puts(" of file ");
	uart_puts(pcFileName);
	uart_puts("\n");
	taskENTER_CRITICAL();
	for (;;);
}

void hello_world_task(void *p)
{
	int i = 0;
	(void)p;
	while (1) {
		printf("%s() %d.\n", __func__, i++);
		vTaskDelay(1000);
	}
}

int main(void)
{
	prvSetupHardware();

	uart_puts("FreeRTOS on PRTOS (HW-Virt) starting!\n");

	/* Run the software timer example */
	test_software_timer();

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should not reach here. */
	for (;;);

	return -1;
}
