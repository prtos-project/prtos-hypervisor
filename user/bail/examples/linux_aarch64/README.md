# Example : linux_aarch64

## Description
This example demonstrates Linux kernel running on PRTOS AArch64 hardware-assisted virtualization (EL2) with 2 vCPUs. It provides a simpler SMP setup compared to the 4-vCPU variant, suitable for quick testing and validation.

## Partition definition
There is one partition.
- P0 (Linux): System partition running Linux with 2 vCPUs using AArch64 EL2 hardware virtualization with GICv3 virtual interface.

## Configuration table
Single-partition SMP configuration with 2 physical CPUs.

A scheduling plan is defined under the following premises:

- Processors: 2 (pCPU 0-1)
- Each pCPU: MAF = 100 ms
- P0 vCPU 0: pCPU 0, S 0 ms  D 50 ms
- P0 vCPU 1: pCPU 1, S 0 ms  D 50 ms

Memory layout:
- PRTOS: 32 MB
- P0 (Linux): 256 MB @ 0x10000000

Boot flow: u-boot → PRTOS → Linux kernel + Buildroot initramfs

## Expected results
PRTOS will load, initialise and run the Linux partition on 2 vCPUs using hardware virtualization.
Linux boots to a login prompt (root/1234). The `htop` command can be used to verify the system status.
