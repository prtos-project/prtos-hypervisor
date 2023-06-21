# Example : example.002

# Description
This example shows how to handle the Health Monitor provided by PRTOS Hypervisor

# Partition definition
There are three partitions.
P1 is a system partition which has access to the Health Monitor log and is able to read it.
P2 and P3 will issue a division by zero which, in turn, will raise the corresponding
Health Monitoring events. 

# Configuration table
P2 has defined a PRTOS_HM_AC_PARTITION_HALT action for the Health Monitor.
P3 has defined a PRTOS_HM_AC_PARTITION_PROPAGATE action for the Health Monitor.

A scheduling plan is defined under the following premises:

MAF = 600 msec 		
P1: S   0 ms  D 200 ms  
P2: S 200 ms  D 200 ms  
P3: S 400 ms  D 200 ms  

# Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P1 will be continuously printing the Health
Monitor log. P2 and P3 will issue divisions by zero and for each
partition, the corresponding action will be executed.

