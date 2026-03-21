/* Standard includes. */
#include <stdio.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"

/* board include files. */
#include "board.h"
#include "gic_v3.h"

/* driver includes. */
#include "uart.h"

#include "example.h"

/* Configure the hardware as necessary */
static void prvSetupHardware( void );

static void prvSetupHardware( void )
{
	/* Ensure no interrupts execute while the scheduler is in an inconsistent
	state.  Interrupts are automatically enabled when the scheduler is
	started. */
	portDISABLE_INTERRUPTS();

	/* Initialize the GIC, including config of partial timer irq */
	gic_v3_initialize();
}

void vMainAssertCalled( const char *pcFileName, uint32_t ulLineNumber )
{
	//xil_printf( "ASSERT!  Line %lu of file %s\r\n", ulLineNumber, pcFileName );
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
	//configASSERT(0);

	/* printf() test, have to enable TEST_PRINTF in uart.h */
	//test_printf();

#if 1	/* Example Test */
	//test_queue();
	//test_semaphore();
	//test_binary_semaphore();
	test_software_timer();
#else
	/* Create Tasks */
	xTaskCreate(hello_world_task, "hello_task", 2048, 0, 1, 0);
#endif

	/* Start the scheduler */	
	vTaskStartScheduler();

	/* Should not reach here. */
	for( ;; );

	return -1;
}
