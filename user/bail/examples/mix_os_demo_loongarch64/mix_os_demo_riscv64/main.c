/*
 * Mixed-OS Demo - FreeRTOS Partition (Partition 1) [RISC-V 64, para-virt]
 *
 * Simulates a real-time motor controller / sensor sampler:
 *   - Motor control task: 1ms period PWM-like control loop
 *   - Sensor sampling task: 10ms period ADC-like sampling
 *   - Watchdog task: periodic heartbeat output
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "uart.h"

/* External references (from freertos_para_virt_riscv) */
extern void _prtos_trap_dispatch(void);
extern unsigned long g_pct_addr;

/* PCT arch trap_entry offset for riscv64 */
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

/* Simulated sensor data */
static volatile unsigned int motor_pwm_duty = 50;
static volatile unsigned int sensor_temp = 250;
static volatile unsigned int sensor_rpm = 3000;
static volatile unsigned int control_loop_count = 0;
static volatile unsigned int sample_count = 0;

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
    for (;;);
}

/*
 * Motor control task - runs at highest priority, 1ms period.
 */
static void motor_control_task(void *p)
{
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(1);

    for (;;) {
        unsigned int error = 3000 - sensor_rpm;
        if (error > 0 && motor_pwm_duty < 100)
            motor_pwm_duty++;
        else if (error == 0)
            ;
        else if (motor_pwm_duty > 0)
            motor_pwm_duty--;

        if (motor_pwm_duty > 50)
            sensor_rpm += 1;
        else if (motor_pwm_duty < 50 && sensor_rpm > 0)
            sensor_rpm -= 1;

        control_loop_count++;
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/*
 * Sensor sampling task - 10ms period.
 */
static void sensor_sample_task(void *p)
{
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(10);

    for (;;) {
        sensor_temp = 250 + (sample_count % 5);
        sample_count++;
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/*
 * Status reporting task - prints periodic status to UART every 2 seconds.
 */
static void status_report_task(void *p)
{
    (void)p;
    unsigned int report_num = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        report_num++;

        uart_puts("[RTOS] Report #");
        uart_puthex(report_num);
        uart_puts("  RPM=");
        uart_puthex(sensor_rpm);
        uart_puts("  Temp=");
        uart_puthex(sensor_temp);
        uart_puts("  PWM=");
        uart_puthex(motor_pwm_duty);
        uart_puts("  CtrlLoops=");
        uart_puthex(control_loop_count);
        uart_puts("  Samples=");
        uart_puthex(sample_count);
        uart_puts("\n");
    }
}

/*
 * Verification task - waits 6 seconds then confirms RTOS is running.
 */
static void verification_task(void *p)
{
    (void)p;
    vTaskDelay(pdMS_TO_TICKS(6000));

    if (control_loop_count > 1000 && sample_count > 100) {
        uart_puts("[RTOS] Verification Passed: Motor ctrl=");
        uart_puthex(control_loop_count);
        uart_puts(" Samples=");
        uart_puthex(sample_count);
        uart_puts("\n");
    } else {
        uart_puts("[RTOS] Verification FAILED\n");
    }

    for (;;)
        vTaskDelay(portMAX_DELAY);
}

int main(void)
{
    prvSetupHardware();

    uart_puts("\n========================================\n");
    uart_puts("[RTOS] FreeRTOS Motor Controller Starting\n");
    uart_puts("[RTOS] Mixed-OS Demo - Partition 1 (RISC-V)\n");
    uart_puts("========================================\n");

    xTaskCreate(motor_control_task, "Motor", 512, NULL,
                configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(sensor_sample_task, "Sensor", 512, NULL,
                configMAX_PRIORITIES - 2, NULL);
    xTaskCreate(status_report_task, "Status", 512, NULL,
                2, NULL);
    xTaskCreate(verification_task,  "Verify", 512, NULL,
                1, NULL);

    vTaskStartScheduler();

    for (;;);
    return -1;
}
