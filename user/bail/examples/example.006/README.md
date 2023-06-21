# Example : example.006

## Description
This example shows how to switch between the cyclic scheduling plans defined in the configuration file. 

## Partition definition
There are three partitions.
- P0 and P1 user partitions.
- P2 is a system partition.

## Configuration table
There have been defined multiple scheduling plans.

A scheduling plan is defined under the following premises:

MAF = 2000 msec 		
- P2: S    0 ms  D 400 ms  
- P0: S  400 ms  D 400 ms  
- P1: S  800 ms  D 400 ms  
- P0: S 1200 ms  D 400 ms  
- P1: S 1600 ms  D 400 ms  

MAF = 1200 msec 		
- P2: S    0 ms  D 400 ms  
- P0: S  400 ms  D 400 ms  
- P1: S  800 ms  D 400 ms  

MAF = 800 msec 		
- P2: S    0 ms  D 400 ms  
- P1: S  400 ms  D 400 ms  

## Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, at each new slot, each partition will write its ID and will idle until the next slot. 

P2 is a system partition and will change the scheduling plan after the MAF has been repeated for 2 times.
Notice that, despite P2 is the first to run, and that it will request a plan change at the beginning of the MAF, the plans will only change at the end of the MAF.  

It must also be noted that plan zero is the initial plan and cannot be called back (it can only be activated by means of a system reset).
Thus, the change to plan zero is expected to fail. The execution never ends.
