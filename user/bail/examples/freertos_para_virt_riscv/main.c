/*
 * FreeRTOS on PRTOS - Para-Virtualization (RISC-V 64)
 * Main entry point.
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "uart.h"
#include "example.h"

/* External references */
extern void _prtos_trap_dispatch(void);
extern unsigned long g_pct_addr;

/* PCT arch structure layout (matches core/include/riscv64/guest.h) */
struct pct_arch_riscv {
	unsigned long irq_saved_pc;
	unsigned long irq_saved_sstatus;
	unsigned long irq_saved_a0;
	unsigned int  irq_vector;
	unsigned int  _pad0;
	unsigned long trap_entry;
};

/* Register the trap dispatch handler in the PCT's arch.trap_entry field.
 * The offset 144 is computed from the partition_control_table_t struct layout
 * for riscv64 (where prtos_u_size_t = u64, prtos_id_t = u64):
 *   offsetof(partition_control_table_t, arch.trap_entry) = 144 */
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
	/* Ensure no interrupts execute while the scheduler is in an inconsistent
	state.  Interrupts are automatically enabled when the scheduler is
	started. */
	portDISABLE_INTERRUPTS();

	/* Initialize UART */
	uart_init();

	/* Register our trap dispatch handler in the PCT */
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

/*
 * Verification task: waits long enough for all 5 software timers to complete
 * (each timer fires 10 times, the slowest at 500ms period = 5 seconds),
 * then prints "Verification Passed" for the automated test framework.
 */
void verification_task(void *p)
{
	(void)p;
	/* Wait 6 seconds - all timers finish within ~5s */
	vTaskDelay(6000 / portTICK_PERIOD_MS);
	uart_puts("Verification Passed\n");
	for (;;)
		vTaskDelay(portMAX_DELAY);
}

int main(void)
{
	prvSetupHardware();

	uart_puts("FreeRTOS on PRTOS (Para-Virt RISC-V) starting!\n");

	/* Run the software timer example */
	test_software_timer();

	/* Verification task for automated testing */
	xTaskCreate(verification_task, "verify", 512, NULL, configMAX_PRIORITIES - 1, NULL);

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should not reach here. */
	for (;;)
		;

	return -1;
}
