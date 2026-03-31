/*
 * Mixed-OS Demo - FreeRTOS Partition (Partition 1) [amd64, para-virt]
 *
 * Simulates a real-time motor controller / sensor sampler:
 *   - Motor control task: 1ms period PWM-like control loop
 *   - Sensor sampling task: 10ms period ADC-like sampling
 *   - Status reporting task: periodic heartbeat output every 2 seconds
 *   - Verification task: confirms RTOS is running after 6 seconds
 *
 * Uses BAIL's printf (via PRTOS write_console hypercall) for output.
 * Timer interrupt routed through IDT vector 224 from PRTOS ext HW timer.
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "uart.h"

/* PRTOS ext IRQ management */
extern int prtos_clear_irqmask(unsigned int hw_irqs_mask, unsigned int ext_irqs_mask);
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

#ifndef GUEST_CS_SEL
#define GUEST_CS_SEL 0x13b
#endif

struct x86_desc_reg {
	unsigned short limit;
	unsigned long linear_base;
} __attribute__((packed));
extern struct x86_desc_reg part_idt_desc;
extern void prtos_x86_load_idtr(struct x86_desc_reg *);

/* Assembly ISR entry points */
extern void _timer_isr(void);

/* From portASM.S / port.c */
extern unsigned long ullPortYieldRequired;
extern void FreeRTOS_Tick_Handler(void);

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

/* Simulated sensor data */
static volatile unsigned int motor_pwm_duty = 50;
static volatile unsigned int sensor_temp = 250;
static volatile unsigned int sensor_rpm = 3000;
static volatile unsigned int control_loop_count = 0;
static volatile unsigned int sample_count = 0;

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
 * Status reporting task - prints periodic status every 2 seconds.
 */
static void status_report_task(void *p)
{
	(void)p;
	unsigned int report_num = 0;

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(2000));
		report_num++;
		printf("[RTOS] Report #%u  RPM=%u  Temp=%u  PWM=%u  CtrlLoops=%u  Samples=%u\n",
			report_num, sensor_rpm, sensor_temp, motor_pwm_duty,
			control_loop_count, sample_count);
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
		printf("[RTOS] Verification Passed: Motor ctrl=%u Samples=%u\n",
			control_loop_count, sample_count);
	} else {
		printf("[RTOS] Verification FAILED\n");
	}

	for (;;)
		vTaskDelay(portMAX_DELAY);
}

/*
 * partition_main - called by BAIL boot.S
 */
void partition_main(void)
{
	portDISABLE_INTERRUPTS();

	/* Install FreeRTOS timer ISR directly in the IDT */
	install_idt_gate(TIMER_IRQ_VECTOR, _timer_isr, 3);
	prtos_x86_load_idtr(&part_idt_desc);

	/* Unmask the hardware timer IRQ in PRTOS */
	prtos_clear_irqmask(0, (1 << PRTOS_VT_EXT_HW_TIMER));

	printf("\n========================================\n");
	printf("[RTOS] FreeRTOS Motor Controller Starting\n");
	printf("[RTOS] Mixed-OS Demo - Partition 1 (amd64)\n");
	printf("========================================\n");

	xTaskCreate(motor_control_task, "Motor", 512, NULL,
		    configMAX_PRIORITIES - 1, NULL);
	xTaskCreate(sensor_sample_task, "Sensor", 512, NULL,
		    configMAX_PRIORITIES - 2, NULL);
	xTaskCreate(status_report_task, "Status", 512, NULL,
		    2, NULL);
	xTaskCreate(verification_task,  "Verify", 512, NULL,
		    1, NULL);

	vTaskStartScheduler();

	for (;;)
		;
}
