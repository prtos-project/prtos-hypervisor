# Example : example.ucosii

# Description
This example shows two UC/OS-II instances are run on each partition provided by PRTOS Hypervisor

# Partition definition
There are two partitions.
P1 will program the timer based on the Hardware clock.
P2 will program the timer based on the Execution clock.

# Configuration table
Basic configuration. Partitions are defined to be executed at different memory addresses.

A scheduling plan is defined under the following premises:

MAF = 1000 msec 		
P1: S   0 ms  D 500 ms  
P2: S 500 ms  D 500 ms  

# Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P1 will install a handler for the timer IRQ
and will set the HW timer based on HW_CLOCK at a period of 1 second.
P2 will install a handler for the timer irq and will set the EXEC timer
based on EXEC clock at a period of 1 second. 

