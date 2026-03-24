/*
 * Mixed-OS Demo - FreeRTOS Partition (Partition 0)
 *
 * Simulates a real-time motor controller / sensor sampler:
 *   - Motor control task: 1ms period PWM-like control loop
 *   - Sensor sampling task: 10ms period ADC-like sampling
 *   - Watchdog task: periodic heartbeat output
 *
 * Demonstrates that FreeRTOS timing is not affected by Linux load
 * on the other partition's CPUs.
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "gic_v3.h"
#include "uart.h"

/* Simulated sensor data (shared concept - output via UART for demo) */
static volatile uint32_t motor_pwm_duty = 50;    /* 0-100% */
static volatile uint32_t sensor_temp = 250;       /* 25.0°C x10 */
static volatile uint32_t sensor_rpm = 3000;       /* RPM */
static volatile uint32_t control_loop_count = 0;
static volatile uint32_t sample_count = 0;

static void prvSetupHardware(void)
{
    portDISABLE_INTERRUPTS();
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
 * Motor control task - runs at highest priority, 1ms period.
 * Simulates a PID control loop for motor speed regulation.
 */
static void motor_control_task(void *p)
{
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(1);

    for (;;) {
        /* Simulate PID calculation */
        uint32_t error = 3000 - sensor_rpm;
        if (error > 0 && motor_pwm_duty < 100)
            motor_pwm_duty++;
        else if (error == 0)
            ; /* steady state */
        else if (motor_pwm_duty > 0)
            motor_pwm_duty--;

        /* Simulate motor response */
        if (motor_pwm_duty > 50)
            sensor_rpm += 1;
        else if (motor_pwm_duty < 50 && sensor_rpm > 0)
            sensor_rpm -= 1;

        control_loop_count++;
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/*
 * Sensor sampling task - runs at medium priority, 10ms period.
 * Simulates ADC readings for temperature monitoring.
 */
static void sensor_sample_task(void *p)
{
    (void)p;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(10);

    for (;;) {
        /* Simulate temperature sensor with small fluctuation */
        sensor_temp = 250 + (sample_count % 5);
        sample_count++;
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/*
 * Status reporting task - prints periodic status to UART.
 * Runs every 2 seconds to show system is alive.
 */
static void status_report_task(void *p)
{
    (void)p;
    uint32_t report_num = 0;

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

    /* Verify that control loops and samples are running */
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
    uart_puts("[RTOS] Mixed-OS Demo - Partition 1\n");
    uart_puts("========================================\n");

    /* Create real-time tasks */
    xTaskCreate(motor_control_task, "Motor", 512, NULL,
                configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(sensor_sample_task, "Sensor", 512, NULL,
                configMAX_PRIORITIES - 2, NULL);
    xTaskCreate(status_report_task, "Status", 512, NULL,
                2, NULL);
    xTaskCreate(verification_task,  "Verify", 512, NULL,
                1, NULL);

    vTaskStartScheduler();

    /* Should not reach here */
    for (;;);
    return -1;
}
