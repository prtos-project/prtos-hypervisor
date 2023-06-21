# Example : example.005

# Description
This example shows how to inject custom files in the partition code.

# Partition definition
There is one partition:
P1 will print the contents of the custom file.

# Configuration table
P1 will override the default XAL header so that it provides the location
of the reserved memory for the custom file.

# Expected results
PRTOS will load, initialise and run in user mode the partitions. 
During the execution, P1 will print the contents of the custom file.
