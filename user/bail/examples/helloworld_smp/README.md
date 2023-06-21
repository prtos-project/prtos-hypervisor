# Example : hello-world-smp

# Description
This example shows how to build a basic application with 1 vcpu on each partion with prtos on 2 cores.

# Partition definition
There is one partition that will print a 'I'm PartitionId:vCPUId,My name is PartitionId' message

# Configuration table
Basic configuration. Partitions are defined to be executed at different memory addresses.

A scheduling plan is defined under the following premises:

MAF = 2000 msec 		
- P0: S   0 ms  D 1000 ms  Core=0
- P1: S   0 ms  D 1000 ms  Core=1

# Expected results
\prtos{} will load, initialise and run in user mode the partitions.
During the execution, the partition on each core will print a message identification and halt.

