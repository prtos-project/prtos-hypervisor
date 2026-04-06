# Example : freertos_hw_virt_amd64

## Description
This example demonstrates FreeRTOS running on PRTOS x86_64 hardware-assisted virtualization using Intel VMX and EPT (Extended Page Tables). The guest FreeRTOS runs unmodified native code.

## Partition definition
There is one partition.
- P0 (FreeRTOS_Native): System partition running a FreeRTOS demo using hardware virtualization.

## Configuration table
Basic single-partition configuration with hardware-assisted virtualization.

A scheduling plan is defined under the following premises:

- Processors: 1 (pCPU 0)
- MAF = 1000 ms
- P0: S 0 ms  D 1000 ms

Memory layout:
- PRTOS: 8 MB
- P0 (FreeRTOS_Native): 64 MB @ 0x6000000

## Expected results
PRTOS will load, initialise and run the FreeRTOS partition using Intel VMX hardware virtualization with EPT memory translation.
During the execution, FreeRTOS will run its demo and output "Verification Passed".
