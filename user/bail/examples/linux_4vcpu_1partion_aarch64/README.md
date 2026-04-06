# Example : linux_4vcpu_1partion_aarch64

## Description
This example demonstrates Linux kernel running on PRTOS AArch64 hardware-assisted virtualization with SMP support (4 vCPUs). It showcases PRTOS multicore scheduling capabilities with a full Linux distribution (Buildroot) booted via u-boot.

## Partition definition
There is one partition.
- P0 (Linux): System partition running Linux with 4 vCPUs, one per physical CPU, using AArch64 EL2 hardware virtualization with GICv3 virtual interface.

## Configuration table
Single-partition SMP configuration with 4 physical CPUs.

A scheduling plan is defined under the following premises:

- Processors: 4 (pCPU 0-3)
- Each pCPU: MAF = 100 ms
- P0 vCPU 0: pCPU 0, S 0 ms  D 100 ms
- P0 vCPU 1: pCPU 1, S 0 ms  D 100 ms
- P0 vCPU 2: pCPU 2, S 0 ms  D 100 ms
- P0 vCPU 3: pCPU 3, S 0 ms  D 100 ms

Memory layout:
- PRTOS: 32 MB
- P0 (Linux): 256 MB @ 0x10000000

Boot flow: u-boot → PRTOS → Linux kernel + Buildroot initramfs

## Expected results
PRTOS will load, initialise and run the Linux partition on 4 vCPUs using hardware virtualization.
Linux boots to a login prompt (root/1234). The `nproc` command verifies that all 4 vCPUs are active.
