# Example : example.006

# Description
This example shows how to switch between the cyclic scheduling plans
defined in the configuration file. 

# Partition definition
There are three partitions.
P1 and P2 user partitions.
P3 is a system partition.

# Configuration table
There have been defined multiple scheduling plans.

A scheduling plan is defined under the following premises:

MAF = 2000 msec 		
P3: S    0 ms  D 400 ms  
P1: S  400 ms  D 400 ms  
P2: S  800 ms  D 400 ms  
P1: S 1200 ms  D 400 ms  
P2: S 1600 ms  D 400 ms  

MAF = 1200 msec 		
P3: S    0 ms  D 400 ms  
P1: S  400 ms  D 400 ms  
P2: S  800 ms  D 400 ms  

MAF = 800 msec 		
P3: S    0 ms  D 400 ms  
P2: S  400 ms  D 400 ms  

# Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, at each new slot, each partition will write its
ID and will idle until the next slot. P3 is a system partition and will
change the scheduling plan after the MAF has been repeated for 5 times.
Notice that, despite P3 is the first to run, and that it will request a
plan change at the beginning of the MAF, the plans will only change at
the end of the MAF.  

