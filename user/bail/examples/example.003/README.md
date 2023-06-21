# Example : example.003

## Description
This example shows how to generate traces and how to manage them

## Partition definition
There are three partitions.
- P0 and P1 will write trace events to the selected devices
- P2 is a system partition and is able to read the traces 

## Configuration table
- P0 is using a MemDisk0 memory block device
- P1 is using a MemDisk1 memory block device
- P2 is system partition

A scheduling plan is defined under the following premises:

MAF = 600 msec 		
- P0: S   0 ms  D 200 ms  
- P1: S 200 ms  D 200 ms  
- P2: S 400 ms  D 200 ms  

## Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P0 and P1 will write trace events to the selected devices. 
P2 will read the generated events, that will also include the information inserted by PRTOS Hypervisor.
