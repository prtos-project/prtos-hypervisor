# Example : example.001

## Description
This example shows how to handle the two types of timers provided by PRTOS Hypervisor

## Partition definition
There are two partitions.
- P1 will program the timer based on the hardware clock.
- P2 will program the timer based on the execution clock.

## Configuration table
Basic configuration. Partitions are defined to be executed at different memory addresses.

A scheduling plan is defined under the following premises:

MAF = 1000 msec 		
- P0: S   0 ms  D 500 ms  
- P1: S 500 ms  D 500 ms  

## Expected results
PRTOS will load, initialise and run partition in user mode. 
During the execution, P1 will install a handler for the timer IRQ and will set the HW timer based on HW_CLOCK at a period of 1 second.
P2 will install a handler for the timer irq and will set the EXEC timer based on EXEC clock at a period of 1 second. 
