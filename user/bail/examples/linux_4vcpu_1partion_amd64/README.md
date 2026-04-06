# Example : linux_4vcpu_1partion_amd64

## Description
This example demonstrates Linux kernel running on PRTOS x86_64 hardware-assisted virtualization (Intel VMX/EPT) with SMP support (4 vCPUs). It tests EPT memory translation under a multicore Linux workload.

## Partition definition
There is one partition.
- P0 (Linux): System partition running Linux with 4 vCPUs using VMX hardware virtualization and EPT memory mapping.

## Configuration table
Single-partition SMP configuration with 4 physical CPUs.

A scheduling plan is defined under the following premises:

- Processors: 4 (pCPU 0-3)
- Each pCPU: MAF = 100 ms
- P0 vCPU 0: pCPU 0, S 0 ms  D 50 ms
- P0 vCPU 1: pCPU 1, S 0 ms  D 50 ms
- P0 vCPU 2: pCPU 2, S 0 ms  D 50 ms
- P0 vCPU 3: pCPU 3, S 0 ms  D 50 ms

Memory layout:
- PRTOS: 8 MB
- P0 (Linux): 256 MB @ 0x6000000

Note: EPT only identity-maps the first 1 GB. All partition memory addresses must be below 0x40000000.

## Expected results
PRTOS will load, initialise and run the Linux partition on 4 vCPUs using Intel VMX hardware virtualization.
Linux boots to a login prompt (root/1234). The `nproc` command verifies that all 4 vCPUs are active.
