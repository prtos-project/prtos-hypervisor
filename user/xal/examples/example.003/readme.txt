# Example : example.003

# Description
This example shows how to use the PRTOS traces

# Partition definition
There are three partitions.
P1 and P2 will write trace events to the selected devices
P3 is a system partition and is able to read the traces 

# Configuration table
P1 is using a MemDisk0 memory block device
P2 is using a MemDisk1 memory block device
P3 is system partition

A scheduling plan is defined under the following premises:

MAF = 600 msec 		
P1: S   0 ms  D 200 ms  
P2: S 200 ms  D 200 ms  
P3: S 400 ms  D 200 ms  

# Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P1 and P2 will write trace events to the
selected devices. P3 will read the generated events, that will 
include also the information inserted by PRTOS Hypervisor.

