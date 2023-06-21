# Example : example.004

## Description
This example shows the functionality of partition's memmory separation and how to use the shared memory for inter-partition communication

## Partition definition
There are three partitions.
- P0 will write to value to the address that that not belonged to its memory, which will be detected by PRTOS' HM and be halted.
- P1 will will write value on shared memory and send a IPI interrupt to P2.
- P2 will read from the shared memory from the IPI interrupt handle.

## Configuration table
- P0 has write access to a memory address which is not belonged to its memory space.
- P1 has acess to shared memory and writes value to this share memory.
- P2 has acess to shared memory.

A scheduling plan is defined under the following premises:

MAF = 1500 msec 		
- P0: S    0 ms  D 500 ms  
- P1: S  500 ms  D 500 ms  
- P2: S 1000 ms  D 500 ms  

## Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P1 will write a memory address which is not belonged to its memory space, which will be detected by PRTOS' HM and be halted.
P2 will write value on shared memory.
P3 will read the shared memory.
All of them will print the messages.

