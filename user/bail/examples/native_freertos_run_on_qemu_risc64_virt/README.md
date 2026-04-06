# Example : native_freertos_run_on_qemu_risc64_virt

## Description
This example runs FreeRTOS natively on QEMU RISC-V 64 virt machine without the PRTOS hypervisor. It serves as a baseline reference for comparing virtualization overhead against the freertos_hw_virt_riscv and freertos_para_virt_riscv examples.

## Partition definition
No PRTOS partitioning — FreeRTOS runs directly on bare-metal QEMU.

## Configuration table
No PRTOS XML configuration. FreeRTOS is built as a standalone ELF image.

- Target: RISC-V 64-bit
- CPU: 1 vCPU
- FreeRTOS port: RISC_V_64_BIT, Heap: heap_1
- Compiler: riscv64-linux-gnu-gcc, -march=rv64gc -mabi=lp64d -mcmodel=medany

## Expected results
FreeRTOS boots directly on QEMU and runs its demo without any hypervisor layer, providing a performance baseline for the virtualized FreeRTOS examples.
