#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

#ifdef CONFIG_x86
prtos_u32_t exception_ret;

// BAIL trap API
void divide_exception_handler(trap_ctxt_t *ctxt) {
    PRINT("#Divide Exception propagated, ignoring...\n");
    ctxt->ip = exception_ret;
}
#endif

#ifdef CONFIG_AARCH64
/*
 * AArch64 trap handler vector number.
 * Trap 0 (UNDEF_INSTR) is routed to this vector via prtos_route_irq.
 * Must be non-zero because irq_vector==0 is the "no pending IRQ" sentinel.
 */
#define UNDEF_INSTR_TRAP 0
#define UNDEF_INSTR_VECTOR 1

void undef_exception_handler(trap_ctxt_t *ctxt) {
    PRINT("#Undefined Instruction Exception propagated, ignoring...\n");
    /* No PC modification needed: IRET returns to after the HVC call */
}
#endif

#ifdef CONFIG_riscv64
#define ILLEGAL_INSTR_TRAP 0
#define ILLEGAL_INSTR_VECTOR 1

void illegal_instr_exception_handler(trap_ctxt_t *ctxt) {
    PRINT("#Illegal Instruction Exception propagated, ignoring...\n");
}
#endif

void partition_main(void) {
    prtos_idle_self();

#ifdef CONFIG_x86
    {
        volatile prtos_s32_t val = 0;

        // Install trap handler
        install_trap_handler(DIVIDE_ERROR, divide_exception_handler);

        __asm__ __volatile__("movl $1f, %0\n\t" : "=r"(exception_ret) :);

        PRINT("Dividing by zero...\n");

        val = 10 / val;

        __asm__ __volatile__("1:\n\t");
    }
#endif

#ifdef CONFIG_AARCH64
    /* Route trap 0 (UNDEF_INSTR) to vector 1 and install handler */
    prtos_route_irq(PRTOS_TRAP_TYPE, UNDEF_INSTR_TRAP, UNDEF_INSTR_VECTOR);
    install_trap_handler(UNDEF_INSTR_VECTOR, undef_exception_handler);

    PRINT("Raising undefined instruction trap...\n");

    /*
     * Para-virtualized trap: on AArch64, EL1 faults don't trap to EL2,
     * so we explicitly notify the hypervisor via HVC.
     * For HALT: partition is halted by HM, never returns.
     * For PROPAGATE: handler runs, IRET returns here.
     */
    prtos_raise_partition_trap(UNDEF_INSTR_TRAP);
#endif

#ifdef CONFIG_riscv64
    /* Route trap 0 (ILLEGAL_INSTR) to vector 1 and install handler */
    prtos_route_irq(PRTOS_TRAP_TYPE, ILLEGAL_INSTR_TRAP, ILLEGAL_INSTR_VECTOR);
    install_trap_handler(ILLEGAL_INSTR_VECTOR, illegal_instr_exception_handler);

    PRINT("Raising illegal instruction trap...\n");

    prtos_raise_partition_trap(ILLEGAL_INSTR_TRAP);
#endif

    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
