/* Standard includes. */
#include <stdio.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"

/* board include files. */
#include "board.h"

/* driver includes. */
#include "uart.h"

#include "example.h"

/* Configure the hardware as necessary */
static void prvSetupHardware( void );

static void prvSetupHardware( void )
{
	portDISABLE_INTERRUPTS();
	uart_init();
}

void vMainAssertCalled( const char *pcFileName, uint32_t ulLineNumber )
{
	uart_puts("ASSERT!  Line ");
	uart_puthex(ulLineNumber);
	uart_puts(" of file ");
	uart_puts( pcFileName );
	uart_puts("\n" );
	taskENTER_CRITICAL();
	for( ;; );
}

void hello_world_task(void *p)
{
	int i=0;

	(void)p;
	while(2) {
		printf("%s() %d.\n", __func__, i++);
		vTaskDelay(1000);
	}
}

int main(void)
{
	/* Configure the hardware ready to run */
	prvSetupHardware();

	uart_puts("Hello World main()!\n");

	test_software_timer();

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should not reach here. */
	for( ;; );

	return -1;
}
