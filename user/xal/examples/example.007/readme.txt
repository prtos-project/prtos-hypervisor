# Example : example.007

# Description

Test the ability to use some specific hypercalls by a supervisor partition and to control the execution of other partitions. 

# Partition definition
This test uses 3 partitions. P2 and P3 (same code partition.c). 
P1 will control the execution of P2 and P3 

P1 and P2 will call the following hypercalls: 
 prtos_suspend_partition,
 prtos_get_partition_status,
 prtos_resume_partition,
 prtos_halt_partition,
 prtos_reset_partition,
 prtos_halt_system, 

which only can be used by supervisor partitions. 
P2 and P3 print a counter and wait for the next slot. 

# Configuration table
Basic configuration.
P1 is supervisor. 
P2 and P3 are no supervisor. 

MAF = 2000 msec		
P1: S    0 ms  D 500 ms
P2: S  500 ms  D 500 ms
P3: S 1000 ms  D 500 ms
P2: S 1500 ms  D 500 ms
P3: S 2000 ms  D 500 ms

# Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P1 will use the supervisor instructions
to suspend, resume, halt and reset to control the execution
of P2 and P3.

