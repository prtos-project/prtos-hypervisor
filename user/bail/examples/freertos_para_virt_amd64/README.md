# Example : freertos_para_virt_amd64

## Description
This example demonstrates FreeRTOS running on PRTOS x86_64 para-virtualization. The guest FreeRTOS sets up its own IDT and uses PRTOS hypercalls (e.g., prtos_clear_irqmask) for hardware IRQ routing via the ext_irq mechanism.

## Partition definition
There is one partition.
- P0 (FreeRTOS): System partition running a FreeRTOS demo using para-virtualization with explicit IDT setup.

## Configuration table
Basic single-partition configuration with para-virtualization.

A scheduling plan is defined under the following premises:

- Processors: 1 (pCPU 0)
- MAF = 2 ms
- P0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 8 MB
- P0 (FreeRTOS): 2 MB @ 0x6000000 (mappedAt: 0x6000000)

## Expected results
PRTOS will load, initialise and run the FreeRTOS partition in para-virtualization mode.
During the execution, FreeRTOS will set up its IDT, configure interrupt routing via hypercalls, and output "Verification Passed".
