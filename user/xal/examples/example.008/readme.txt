# Example : example.008

# Description
This example shows shows how to build a basic application with 4 vCPUs on each partion with prtos on 4 cores.


# Partition definition
There is two partitions that will print a 'I'm (PartId:vCpuId), My name is PartitionName' message

# Configuration table
Basic configuration. Partitions are defined to be executed at different memory addresses.

A scheduling plan is defined under the following premises:

MAF = 2000 msec 		
P0: S   0 ms     D 500 ms vCPU0 Core=0
P1: S   500  ms  D 500 ms vCPU0 Core=0


MAF = 2000 msec 		
P0: S   0 ms     D 500 ms vCPU1 Core=1
P1: S   500  ms  D 500 ms vCPU1 Core=1



# Expected results
PRTOS will load, initialise and run in user mode the partitions.
During the execution, the partition on each core will print a message identification
and halt.

