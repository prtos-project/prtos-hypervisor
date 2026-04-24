# Example : mix_os_demo_riscv64

## Description
This example demonstrates PRTOS mixed-criticality isolation on RISC-V 64 with heterogeneous workloads: Linux using hardware-assisted virtualization (H-extension, 3 vCPUs) and FreeRTOS using para-virtualization (1 vCPU). Linux receives UART device pass-through for console access.

## Partition definition
There are two partitions.
- P0 (Linux_HMI): System partition running Linux with 3 vCPUs on pCPU 0-2, using H-extension hardware virtualization with UART pass-through.
- P1 (FreeRTOS_RTOS): System partition running FreeRTOS with 1 vCPU on pCPU 3, using para-virtualization.

## Configuration table
Dual-partition mixed-OS configuration with mixed virtualization modes.

A scheduling plan is defined under the following premises:

- Processors: 4 (pCPU 0-3)
- pCPU 0: MAF = 100 ms, P0 vCPU 0: S 0 ms  D 50 ms
- pCPU 1: MAF = 100 ms, P0 vCPU 1: S 0 ms  D 50 ms
- pCPU 2: MAF = 100 ms, P0 vCPU 2: S 0 ms  D 50 ms
- pCPU 3: MAF = 2 ms,   P1 vCPU 0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 64 MB
- P0 (Linux_HMI): 256 MB @ 0x90000000
- P1 (FreeRTOS_RTOS): 2 MB @ 0x88000000
- UART device pass-through: 4 KB @ 0x10000000 (Linux partition)

## Expected results
PRTOS will load, initialise and run both partitions.
Linux boots on pCPU 0-2 using H-extension hardware virtualization and presents a login prompt.
FreeRTOS runs the real-time control loop on pCPU 3 using para-virtualization with guaranteed isolation.
