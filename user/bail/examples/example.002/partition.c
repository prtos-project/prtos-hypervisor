#include <string.h>  // string services provided by the libbail library
#include <stdio.h>   // stdio services provided by the libbail library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libbail library

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

prtos_u32_t exception_ret;

// BAIL trap API
void divide_exception_handler(trap_ctxt_t *ctxt) {
    PRINT("#Divide Exception propagated, ignoring...\n");
    ctxt->ip = exception_ret;
}

void partition_main(void) {
    volatile prtos_s32_t val = 0;

    prtos_idle_self();

#ifdef CONFIG_x86
    // Install timer handler
    install_trap_handler(DIVIDE_ERROR, divide_exception_handler);
#endif

    __asm__ __volatile__("movl $1f, %0\n\t" : "=r"(exception_ret) :);

    PRINT("Dividing by zero...\n");

    val = 10 / val;

    __asm__ __volatile__("1:\n\t");

    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
