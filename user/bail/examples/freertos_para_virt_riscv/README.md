# Example : freertos_para_virt_riscv

## Description
This example demonstrates FreeRTOS running on PRTOS RISC-V 64 para-virtualization. The guest FreeRTOS uses SBI-based para-virtualization interface for interrupt and timer management, with UART device pass-through.

## Partition definition
There is one partition.
- P0 (FreeRTOS): System partition running a FreeRTOS demo using para-virtualization with SBI calls.

## Configuration table
Basic single-partition configuration with para-virtualization and device pass-through.

A scheduling plan is defined under the following premises:

- Processors: 1 (pCPU 0)
- MAF = 2 ms
- P0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 32 MB
- P0 (FreeRTOS): 2 MB @ 0x86000000
- UART device pass-through: 4 KB @ 0x10000000

## Expected results
PRTOS will load, initialise and run the FreeRTOS partition in para-virtualization mode.
During the execution, FreeRTOS will run its demo using SBI para-virtualization calls and output "Verification Passed".
