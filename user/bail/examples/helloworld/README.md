# Example : helloworld

## Description
This example shows how to build a basic application with PRTOS Hypervisor

## Partition definition
There is one partition that will print a 'Hello World!' message

## Configuration table
Basic configuration. Partitions are defined to be executed at different memory addresses.

A scheduling plan is defined under the following premises:

MAF = 200 msec 		
- P1: S   0 ms  D 200 ms  

## Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, the partition will print a 'Hello World!' message and halt.

