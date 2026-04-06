# Example : native_freertos_run_on_qemu_amd64_virt

## Description
This example runs FreeRTOS natively on QEMU x86_64 virt machine without the PRTOS hypervisor. It serves as a baseline reference for comparing virtualization overhead against the freertos_hw_virt_amd64 and freertos_para_virt_amd64 examples.

## Partition definition
No PRTOS partitioning — FreeRTOS runs directly on bare-metal QEMU.

## Configuration table
No PRTOS XML configuration. FreeRTOS is built as a standalone ELF image.

- Target: x86_64 (AMD64)
- CPU: 1 vCPU
- FreeRTOS port: X86_64_BIT, Heap: heap_1
- Compiler: gcc with -m64 -mcmodel=kernel

## Expected results
FreeRTOS boots directly on QEMU and runs its demo without any hypervisor layer, providing a performance baseline for the virtualized FreeRTOS examples.
