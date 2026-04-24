# Example : freertos_para_virt_loongarch64

## Description
This example demonstrates FreeRTOS running on PRTOS LoongArch64 para-virtualization. The guest FreeRTOS uses PRTOS hypercall-based para-virtualization interface for interrupt and timer management, with UART console output via hypercalls.

## Partition definition
There is one partition.
- P0 (FreeRTOS): System partition running a FreeRTOS demo using para-virtualization with PRTOS hypercalls.

## Configuration table
Basic single-partition configuration with para-virtualization.

A scheduling plan is defined under the following premises:

- Processors: 1 (pCPU 0)
- MAF = 2 ms
- P0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 32 MB
- P0 (FreeRTOS): 2 MB @ 0x06000000

## Expected results
PRTOS will load, initialise and run the FreeRTOS partition in para-virtualization mode.
During the execution, FreeRTOS will run its demo using PRTOS hypercalls and output "Verification Passed".
