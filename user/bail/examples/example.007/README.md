# Example : example.007

## Description
Test the ability to use some specific hypercalls by a supervisor partition and to control the execution of other partitions. 

## Partition definition
This test uses 3 partitions. P1 and P2 (same code partition.c). 
P0 will control the execution of P1 and P2 

P0 will call the following hypercalls:
- prtos_suspend_partition,
- prtos_get_partition_status,
- prtos_resume_partition,
- prtos_halt_partition,
- prtos_reset_partition,
- prtos_halt_system, 

which only can be used by supervisor partitions. 
P1 and P2 print a counter and wait for the next slot. 

## Configuration table
Basic configuration.
P0 is supervisor. 
P1 and P2 are no supervisor. 

MAF = 2500 msec
- P0: S    0 ms  D 500 ms
- P1: S  500 ms  D 500 ms
- P2: S 1000 ms  D 500 ms
- P1: S 1500 ms  D 500 ms
- P2: S 2000 ms  D 500 ms

## Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P0 will use the hypercall API to suspend, resume, halt and reset to control the execution of P2 and P3.

