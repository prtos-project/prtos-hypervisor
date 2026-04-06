# Example : native_freertos_run_on_qemu_a57_virt

## Description
This example runs FreeRTOS natively on QEMU ARM Cortex-A57 virt machine without the PRTOS hypervisor. It serves as a baseline reference for comparing virtualization overhead against the freertos_hw_virt_aarch64 and freertos_para_virt_aarch64 examples.

## Partition definition
No PRTOS partitioning — FreeRTOS runs directly on bare-metal QEMU.

## Configuration table
No PRTOS XML configuration. FreeRTOS is built as a standalone ELF image.

- Target: ARM Cortex-A57 (AArch64)
- CPU: 1 vCPU
- FreeRTOS port: ARM_CA57_64_BIT, Heap: heap_1
- Compiler: aarch64-linux-gnu-gcc, -mcpu=cortex-a57

## Expected results
FreeRTOS boots directly on QEMU and runs its software timer demo without any hypervisor layer, providing a performance baseline for the virtualized FreeRTOS examples.
