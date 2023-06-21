# Example : example.004

## Description
This example shows how to use the channels and the shared memory for inter-partition communication

## Partition definition
There are three partitions.
- P0 will write to the queuing and sampling channels.
- P1 will read from the queueing and sampling channels and will write on shared memory.
- P2 will read from the sampling channel and from the shared memory

## Configuration table
- P0 has write access to a portQ queuing port and a portS sampling port.
- P1 has read access to a portQ queuing port and a portS sampling port and has acess to shared memory.
- P2 has read access to a portS sampling port and has acess to shared memory.

A scheduling plan is defined under the following premises:

MAF = 1500 msec 		
- P0: S    0 ms  D 500 ms  
- P1: S  500 ms  D 500 ms  
- P2: S 1000 ms  D 500 ms  

## Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P1 will write a sampling message and a queuing message.
P2 will read both queuing and sampling messages and will write the received queuing message on shared memory. 
P3 will read the sampling message and the shared memory. 
All of them will print the messages. 

