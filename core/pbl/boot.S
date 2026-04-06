/*
 * FILE: boot.S
 *
 * Generic boot code, sets up a valid stack and jumps to the C code
 *
 * http://www.prtos.org/
 */

#include <config.h>
#define MIN_STACK_FRAME 0x60
#define STACK_SIZE 8192


#ifdef CONFIG_x86
.global _start, start
_start:
start:
        movl %ebx, part_ctrl_table_ptr
        movl $end_stack, %esp
        call main_pbl
        movl part_ctrl_table_ptr, %ebx
        jmp *%eax
1:
        jmp 1b
.bss
.global start_stack
start_stack:
        .zero STACK_SIZE
.global end_stack
end_stack:
#endif

#ifdef CONFIG_amd64
.global _start, start
_start:
start:
        movq %rbx, part_ctrl_table_ptr(%rip)
        leaq end_stack(%rip), %rsp
        call main_pbl
        movq part_ctrl_table_ptr(%rip), %rbx
        jmp *%rax
1:
        jmp 1b
.bss
.global start_stack
start_stack:
        .zero STACK_SIZE
.global end_stack
end_stack:
#endif

#ifdef CONFIG_AARCH64
.global _start, start
_start:
start:
        /* x0 = pointer to partition_control_table provided by hypervisor */
        adrp    x1, part_ctrl_table_ptr
        str     x0, [x1, :lo12:part_ctrl_table_ptr]

        adrp    x1, end_stack
        add     sp, x1, :lo12:end_stack

        bl      main_pbl
        /* main_pbl returns entry point in x0 */
        mov     x4, x0

        adrp    x1, part_ctrl_table_ptr
        ldr     x0, [x1, :lo12:part_ctrl_table_ptr]

        br      x4
1:
        wfe
        b       1b

.bss
.global start_stack
start_stack:
        .zero STACK_SIZE
.global end_stack
end_stack:
#endif

#ifdef CONFIG_riscv64
.global _start, start
_start:
start:
        /* a0 = pointer to partition_control_table provided by hypervisor */
        la      t0, part_ctrl_table_ptr
        sd      a0, 0(t0)

        la      sp, end_stack

        call    main_pbl
        /* main_pbl returns entry point in a0 */
        mv      t0, a0

        la      t1, part_ctrl_table_ptr
        ld      a0, 0(t1)

        jr      t0
1:
        wfi
        j       1b

.bss
.global start_stack
start_stack:
        .zero STACK_SIZE
.global end_stack
end_stack:
#endif
