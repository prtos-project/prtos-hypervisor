# Example : mix_os_demo_amd64

## Description
This example demonstrates PRTOS mixed-criticality isolation on x86_64 with heterogeneous workloads and mixed virtualization modes: Linux using hardware-assisted virtualization (VMX) and FreeRTOS using para-virtualization, each on a dedicated physical CPU.

## Partition definition
There are two partitions.
- P0 (Linux): System partition running Linux with 1 vCPU on pCPU 0, using VMX hardware virtualization.
- P1 (FreeRTOS): System partition running FreeRTOS with 1 vCPU on pCPU 1, using para-virtualization.

## Configuration table
Dual-partition mixed-OS configuration with mixed virtualization modes.

A scheduling plan is defined under the following premises:

- Processors: 2 (pCPU 0-1)
- pCPU 0: MAF = 100 ms, P0 vCPU 0: S 0 ms  D 50 ms
- pCPU 1: MAF = 2 ms,   P1 vCPU 0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 8 MB
- P0 (Linux): 256 MB @ 0x6000000
- P1 (FreeRTOS): 2 MB @ 0x4000000 (mappedAt: 0x4000000)

Note: EPT only identity-maps the first 1 GB. All partition memory addresses must be below 0x40000000.

## Expected results
PRTOS will load, initialise and run both partitions.
Linux boots on pCPU 0 using VMX hardware virtualization and presents a login prompt.
FreeRTOS runs the real-time control loop on pCPU 1 using para-virtualization with guaranteed isolation from Linux scheduling.
