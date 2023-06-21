# Example : example.005

## Description
This example shows how to inject custom files in the partition code.

## Partition definition
There is one partition:
- P0 will print the contents of the custom file.

## Configuration table
- P0 will override the default BAIL header so that it provides the location of the reserved memory for the custom file.

MAF = 200 msec
- P0: S   0 ms  D 200 ms

## Expected results
PRTOS will load, initialise and run in user mode the partitions. During the execution, P0 will print the contents of the custom file.
