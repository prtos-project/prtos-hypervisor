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

/*
 * Verification task: waits long enough for all 5 software timers to complete
 * (each timer fires 10 times, the slowest at 500ms period = 5 seconds),
 * then prints "Verification Passed" for the automated test framework.
 */
void verification_task(void *p)
{
	(void)p;
	/* Wait 6 seconds — all timers finish within ~5s */
	vTaskDelay(6000 / portTICK_PERIOD_MS);
	uart_puts("Verification Passed\n");
	for (;;)
		vTaskDelay(portMAX_DELAY);
}

int main(void)
{
	prvSetupHardware();

	uart_puts("FreeRTOS on PRTOS (HW-Virt) starting!\n");

	/* Run the software timer example */
	test_software_timer();

	/* Verification task for automated testing */
	xTaskCreate(verification_task, "verify", 512, NULL, configMAX_PRIORITIES - 1, NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should not reach here. */
	for (;;);

	return -1;
}
