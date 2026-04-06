# Example : mix_os_demo_aarch64

## Description
This example demonstrates PRTOS mixed-criticality isolation on AArch64 with heterogeneous workloads: Linux for HMI (3 vCPUs) and FreeRTOS for real-time control (1 vCPU). Both partitions use hardware-assisted virtualization (EL2) and communicate via shared memory.

## Partition definition
There are two partitions.
- P0 (Linux_HMI): System partition running Linux with 3 vCPUs on pCPU 0-2, serving as the HMI/cloud application.
- P1 (FreeRTOS_RTOS): System partition running FreeRTOS with 1 vCPU on pCPU 3, serving as the real-time motor controller.

Both partitions share 1 MB of memory @ 0x30000000 for inter-partition communication.

## Configuration table
Dual-partition mixed-OS configuration with asymmetric scheduling.

A scheduling plan is defined under the following premises:

- Processors: 4 (pCPU 0-3)
- pCPU 0: MAF = 100 ms, P0 vCPU 0: S 0 ms  D 50 ms
- pCPU 1: MAF = 100 ms, P0 vCPU 1: S 0 ms  D 50 ms
- pCPU 2: MAF = 100 ms, P0 vCPU 2: S 0 ms  D 50 ms
- pCPU 3: MAF = 2 ms,   P1 vCPU 0: S 0 ms  D 1 ms

Memory layout:
- PRTOS: 64 MB
- P0 (Linux_HMI): 256 MB @ 0x10000000
- P1 (FreeRTOS_RTOS): 64 MB @ 0x6000000
- Shared memory: 1 MB @ 0x30000000 (accessible to both partitions)

## Expected results
PRTOS will load, initialise and run both partitions using hardware virtualization.
Linux boots on pCPU 0-2 and presents a login prompt (root/1234).
FreeRTOS runs the real-time motor control loop on pCPU 3 with guaranteed 1 ms response time.
The shared memory region allows FreeRTOS-to-Linux communication while isolation guarantees real-time execution.
