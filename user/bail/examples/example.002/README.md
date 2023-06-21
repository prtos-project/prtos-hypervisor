# Example : example.002

## Description
This example shows how to handle the Health Monitor provided by PRTOS Hypervisor

## Partition definition
There are three partitions.
- P2 is a system partition which has access to the Health Monitor log and is able to read it.
- P0 and P1 will issue a division by zero which, in turn, will raise the corresponding
Health Monitoring events. 

## Configuration table
- P0 has defined a PRTOS_HM_AC_PARTITION_HALT action for the Health Monitor.
- P1 has defined a PRTOS_HM_AC_PARTITION_PROPAGATE action for the Health Monitor.

A scheduling plan is defined under the following premises:

MAF = 600 msec 		
- P0: S   0 ms  D 200 ms  
- P1: S 200 ms  D 200 ms  
- P2: S 400 ms  D 200 ms  

## Expected results
PRTOS will load, initialise and run in user mode partitions P0 and P1, and in system mode partition P2.
During the execution, P2 will be continuously printing the Health Monitor log.
P0 will generate a division by zero and will perform the action configured in its PRTOS configuration file, which is to halt the partition.
P1 will generate a division by zero and will perform the action configured in its PRTOS configuration file, which is to propagate the event. 
A handler will catch this exception and skip to the division instruction, and go to perform a partition halt.
