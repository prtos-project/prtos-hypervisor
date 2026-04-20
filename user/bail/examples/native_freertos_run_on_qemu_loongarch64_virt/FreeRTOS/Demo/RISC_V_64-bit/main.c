/* Standard includes. */
#include <stdio.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"

/* Board include files. */
#include "board.h"

/* Driver includes. */
#include "uart.h"

#include "example.h"

/* Configure the hardware as necessary */
static void prvSetupHardware(void)
{
	/* Ensure no interrupts execute while the scheduler is in an inconsistent
	state.  Interrupts are automatically enabled when the scheduler is
	started. */
	portDISABLE_INTERRUPTS();

	/* Initialize UART */
	uart_init();
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
	/* Configure the hardware ready to run */
	prvSetupHardware();

	uart_puts("Hello World main()!\n");

#if 1	/* Example Test */
	test_software_timer();
#else
	/* Create Tasks */
	xTaskCreate(hello_world_task, "hello_task", 2048, 0, 1, 0);
#endif

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should not reach here. */
	for (;;)
		;

	return -1;
}
