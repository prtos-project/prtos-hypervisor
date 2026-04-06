# Example : freertos_hw_virt_riscv

## Description
This example demonstrates FreeRTOS running on PRTOS RISC-V 64 hardware-assisted virtualization using the H-extension (Hypervisor extension). The guest FreeRTOS runs unmodified native code with UART device pass-through.

## Partition definition
There is one partition.
- P0 (FreeRTOS_Native): System partition running a FreeRTOS demo using hardware virtualization with direct UART access.

## Configuration table
Basic single-partition configuration with hardware-assisted virtualization and device pass-through.

A scheduling plan is defined under the following premises:

- Processors: 1 (pCPU 0)
- MAF = 2 ms
- P0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 32 MB
- P0 (FreeRTOS_Native): 64 MB @ 0x86000000
- UART device pass-through: 4 KB @ 0x10000000

## Expected results
PRTOS will load, initialise and run the FreeRTOS partition using RISC-V H-extension hardware virtualization.
During the execution, FreeRTOS will run its demo with native UART device access and output "Verification Passed".
