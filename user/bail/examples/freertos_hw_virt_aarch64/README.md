# Example : freertos_hw_virt_aarch64

## Description
This example demonstrates FreeRTOS running on PRTOS AArch64 hardware-assisted virtualization (EL2). The guest FreeRTOS runs unmodified native code, with GICv3 emulation handled via stage-2 data abort trapping by the hypervisor.

## Partition definition
There is one partition.
- P0 (FreeRTOS): System partition running a FreeRTOS software timer demo using hardware virtualization.

## Configuration table
Basic single-partition configuration with hardware-assisted virtualization.

A scheduling plan is defined under the following premises:

- Processors: 1 (pCPU 0)
- MAF = 2 ms
- P0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 32 MB
- P0 (FreeRTOS): 2 MB @ 0x6000000

## Expected results
PRTOS will load, initialise and run the FreeRTOS partition using AArch64 EL2 hardware virtualization.
During the execution, FreeRTOS will run the software timer demo and output "Verification Passed" after the timer tests complete.
