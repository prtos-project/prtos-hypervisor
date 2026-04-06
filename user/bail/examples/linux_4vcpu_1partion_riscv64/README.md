# Example : linux_4vcpu_1partion_riscv64

## Description
This example demonstrates Linux kernel running on PRTOS RISC-V 64 hardware-assisted virtualization (H-extension) with SMP support (4 vCPUs). It showcases the RISC-V Hypervisor extension capabilities for running a full SMP Linux guest.

## Partition definition
There is one partition.
- P0 (Linux): System partition running Linux with 4 vCPUs using RISC-V H-extension hardware virtualization, with UART device pass-through.

## Configuration table
Single-partition SMP configuration with 4 physical CPUs and device pass-through.

A scheduling plan is defined under the following premises:

- Processors: 4 (pCPU 0-3)
- Each pCPU: MAF = 100 ms
- P0 vCPU 0: pCPU 0, S 0 ms  D 50 ms
- P0 vCPU 1: pCPU 1, S 0 ms  D 50 ms
- P0 vCPU 2: pCPU 2, S 0 ms  D 50 ms
- P0 vCPU 3: pCPU 3, S 0 ms  D 50 ms

Memory layout:
- PRTOS: 64 MB
- P0 (Linux): 256 MB @ 0x88000000
- UART device pass-through: 4 KB @ 0x10000000

## Expected results
PRTOS will load, initialise and run the Linux partition on 4 vCPUs using RISC-V H-extension hardware virtualization.
Linux boots to a login prompt (root/1234). The `nproc` command verifies that all 4 vCPUs are active.
