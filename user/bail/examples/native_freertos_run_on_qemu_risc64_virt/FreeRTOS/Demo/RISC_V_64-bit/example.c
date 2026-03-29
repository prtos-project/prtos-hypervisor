#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "uart.h"
#include "timers.h"
#include "example.h"


/* Queue Example */
static xQueueHandle Global_Queue_Handle = 0;

void sender_task(void *p)
{
	(void)p;
	int i=0;
	while(1){
		printf("Send %d to receiver task.\n", i);
		if(!xQueueSend(Global_Queue_Handle, &i, 1000)){
			printf("Failed to send to queue.\n");
		}
		i++;
		vTaskDelay(3000);
	}
}

void receive_task(void *p)
{
	int rx_int=0;

	(void)p;
	while(1){
		if(xQueueReceive(Global_Queue_Handle, &rx_int, 1000)){
			printf("Received %d.\n", rx_int);
		}else{
			printf("Failed to receive from queue.\n");
		}
	}
}

void test_queue(void)
{
	Global_Queue_Handle = xQueueCreate(3, sizeof(int));

	/* Create Tasks */
	xTaskCreate(sender_task, "tx", 1024, NULL, 1, NULL);
	xTaskCreate(receive_task, "rx", 1024, NULL, 1, NULL);
}

/* Mutex Example */
static xSemaphoreHandle gatekeeper = 0;
static void access_precious_resource(void){}

void user_1(void *p)
{
	(void)p;
	while(1){
		if(xSemaphoreTake(gatekeeper, 1000)){
			printf("User 1 got access.\n");
			access_precious_resource();
			xSemaphoreGive(gatekeeper);
		}else{
			printf("User 1 failed to get access within 1 second.\n");
		}
	vTaskDelay(1000);
	}
}

void user_2(void *p)
{
	(void)p;
	while(1){
		if(xSemaphoreTake(gatekeeper, 1000)){
			printf("User 2 got access.\n");
			access_precious_resource();
			xSemaphoreGive(gatekeeper);
		}else{
			printf("User 2 failed to get access within 1 second.\n");
		}
	vTaskDelay(1000);
	}
}

void test_semaphore(void)
{
	gatekeeper = xSemaphoreCreateMutex();

	/* Create Tasks */
	xTaskCreate(user_1, "t1", 1024, NULL, 1, NULL);
	xTaskCreate(user_2, "t2", 1024, NULL, 1, NULL);
}

/* Binary Semaphore Example */
static xSemaphoreHandle employee_signal = 0;
static void	employee_task(){}

void boss(void *p)
{
	(void)p;
	while(1){
		printf("Boss give the signal.\n");
		xSemaphoreGive(employee_signal);
		printf("Boss finished givin the signal.\n");
		vTaskDelay(2000);
	}
}

void employee(void *p)
{
	(void)p;
	while(1){
		if(xSemaphoreTake(employee_signal, portMAX_DELAY)){
			employee_task();
			printf("Employee has finished its task.\n");
		}
	}
}

void test_binary_semaphore(void)
{
	employee_signal = xSemaphoreCreateBinary();

	/* Create Tasks */
	/* Change the priority will affect the working flow */
	xTaskCreate(boss, "boss", 1024, NULL, 1, NULL);
	//xTaskCreate(employee, "employee", 1024, NULL, 1, NULL);
	xTaskCreate(employee, "employee", 1024, NULL, 2, NULL);
}

/* Software Timer Example */
#define NUM_TIMERS 5
/* An array to hold handles to the created timers. */
TimerHandle_t xTimers[ NUM_TIMERS ];

/* Define a callback function that will be used by multiple timer
instances.  The callback function does nothing but count the number
of times the associated timer expires, and stop the timer once the
timer has expired 10 times.  The count is saved as the ID of the
timer. */
void vTimerCallback( TimerHandle_t pxTimer )
{
	const uint32_t ulMaxExpiryCountBeforeStopping = 10;
	uint64_t ulCount;

	/* Optionally do something if the pxTimer parameter is NULL. */
	configASSERT( pxTimer );

	/* The number of times this timer has expired is saved as the
	timer's ID.  Obtain the count. */
	ulCount = ( uint64_t ) pvTimerGetTimerID( pxTimer );

	/* Increment the count, then test to see if the timer has expired
	ulMaxExpiryCountBeforeStopping yet. */
	ulCount++;

	/* If the timer has expired 10 times then stop it from running. */
	if( ulCount >= ulMaxExpiryCountBeforeStopping )
	{
		/* Do not use a block time if calling a timer API function
		from a timer callback function, as doing so could cause a
		deadlock! */
		xTimerStop( pxTimer, 0 );
		printf("%s() Timer:%X Stop\n", __func__, pxTimer);
	} else {
		/* Store the incremented count back into the timer's ID field
		so it can be read back again the next time this software timer
		expires. */
		vTimerSetTimerID( pxTimer, ( void * ) ulCount );
	}
}

void TaskA(void *p)
{
	(void) p;
	while(1){
		printf("%s() ticks:%d\n", __func__, xTaskGetTickCount());
		vTaskDelay(500 / portTICK_RATE_MS);
	}
}

void test_software_timer( void )
{
	long x;

	printf("%s()\n", __func__);
	xTaskCreate(TaskA, "Task A", 512, NULL, tskIDLE_PRIORITY, NULL);

	/* Create then start some timers.  Starting the timers before
	the RTOS scheduler has been started means the timers will start
	running immediately that the RTOS scheduler starts. */
	for( x = 0; x < NUM_TIMERS; x++ ) {
		xTimers[ x ] = xTimerCreate (
		 "Timer",			/* Just a text name, not used by the RTOS kernel. */
		 ( 100 * x ) + 100,	/* The timer period in ticks, must be greater than 0. */
		 pdTRUE,			/* The timers will auto-reload themselves when they expire. */
		 ( void * ) 0,		/* The ID is used to store a count of the number of times the timer has expired, which is initialised to 0. */
		 vTimerCallback		/* Each timer calls the same callback when it expires. */
		);

		if( xTimers[ x ] == NULL ) {
			/* The timer was not created. */
			printf("Timer[%d] failed to create.\n", x);
		} else {
			/* Start the timer.  No block time is specified, and
			even if one was it would be ignored because the RTOS
			scheduler has not yet been started. */
			if( xTimerStart( xTimers[ x ], 0 ) != pdPASS ) {
				/* 	The timer could not be set into the Active state. */
				printf("Timer[%d] could not be set into the Active state.\n", x);
			}else{
				printf("Timer[%d]:%X is set into the Active state.\n", x, xTimers[x]);
			}
		}
	}
}
