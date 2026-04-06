# Example : freertos_para_virt_aarch64

## Description
This example demonstrates FreeRTOS running on PRTOS AArch64 para-virtualization. The guest FreeRTOS uses PRTOS hypercalls for interrupt management and timer programming, sharing the GIC interface setup with the hypervisor.

## Partition definition
There is one partition.
- P0 (FreeRTOS): System partition running a FreeRTOS software timer demo using para-virtualization hypercalls.

## Configuration table
Basic single-partition configuration with para-virtualization.

A scheduling plan is defined under the following premises:

- Processors: 1 (pCPU 0)
- MAF = 2 ms
- P0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 32 MB
- P0 (FreeRTOS): 2 MB @ 0x6000000

## Expected results
PRTOS will load, initialise and run the FreeRTOS partition in para-virtualization mode.
During the execution, FreeRTOS will run the software timer demo using hypercall-based interrupt management and output "Verification Passed".
