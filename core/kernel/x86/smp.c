/*
 * FILE: smp.c
 *
 * Symmetric multi-processor
 *
 * www.prtos.org
 */
#include <arch/paging.h>
#include <assert.h>
#include <boot.h>
#include <sched.h>
#include <smp.h>

#define SMP_RM_START_ADDR 0x1000

#ifdef CONFIG_SMP

struct x86_mp_conf x86_mp_conf;
extern prtos_s32_t u_delay(prtos_u32_t usec);
extern void setup_cpu_idtable(prtos_u32_t num_of_cpus);
extern void init_smp_acpi(void);
extern void init_smp_mpspec(void);

prtos_s32_t __VBOOT init_smp(void) {
    memset(&x86_mp_conf, 0, sizeof(struct x86_mp_conf));
#ifdef CONFIG_SMP_INTERFACE_ACPI
    init_smp_acpi();
#endif
#ifdef CONFIG_SMP_INTERFACE_MPSPEC
    init_smp_mpspec();
#endif
    return x86_mp_conf.num_of_cpu;
}

static prtos_u32_t apic_wait_for_delivery(void) {
    prtos_u32_t timeout, send_pending;

    send_pending = 1;
    for (timeout = 0; timeout < 1000 && (send_pending != 0); timeout++) {
        u_delay(100);
        send_pending = lapic_read(APIC_ICR_LOW) & APIC_ICR_BUSY;
    }

    return send_pending;
}

static void wake_up_ap(prtos_u32_t start_eip, prtos_u32_t cpu_id) {
    prtos_u32_t i, send_status, accept_status, max_lvt;

    lapic_write(APIC_ICR_HIGH, SET_APIC_DEST_FIELD(cpu_id));
    lapic_write(APIC_ICR_LOW, APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT);
    apic_wait_for_delivery();
    u_delay(10000);
    lapic_write(APIC_ICR_HIGH, SET_APIC_DEST_FIELD(cpu_id));
    lapic_write(APIC_ICR_LOW, APIC_INT_LEVELTRIG | APIC_DM_INIT);
    apic_wait_for_delivery();

    max_lvt = lapic_get_max_lvt();

    for (i = 0; i < 2; i++) {
        lapic_write(APIC_ESR, 0);
        lapic_read(APIC_ESR);
        lapic_write(APIC_ICR_HIGH, SET_APIC_DEST_FIELD(cpu_id));
        lapic_write(APIC_ICR_LOW, APIC_DM_STARTUP | (start_eip >> 12));
        u_delay(300);
        send_status = apic_wait_for_delivery();
        u_delay(200);

        if (max_lvt > 3) {
            lapic_read(APIC_SVR);
            lapic_write(APIC_ESR, 0);
        }

        accept_status = lapic_read(APIC_ESR) & 0xEF;
        if (send_status || accept_status) {
            break;
        }
    }
}

static inline void __VBOOT setup_ap_stack(prtos_u32_t ncpu) {
    extern struct {
        volatile prtos_u32_t *esp;
        volatile prtos_u16_t ss;
    } _sstack;
    prtos_u32_t *ptr;

    ptr = (prtos_u32_t *)((prtos_u32_t)_sstack.esp + CONFIG_KSTACK_SIZE);
    *(--ptr) = (prtos_u32_t)_sstack.esp;
    *(--ptr) = ncpu;
    _sstack.esp = ptr;
}

void __VBOOT setup_smp(void) {
    extern const prtos_u8_t smp_start16[], smp_start16_end[];
    extern volatile prtos_u8_t ap_ready[];
    prtos_u32_t start_eip, ncpu;

    start_eip = SMP_RM_START_ADDR;

    flush_tlb();
    SET_NRCPUS((GET_NRCPUS() < prtos_conf_table.hpv.num_of_cpus) ? GET_NRCPUS() : prtos_conf_table.hpv.num_of_cpus);
    if (GET_NRCPUS() > 1) {
        setup_cpu_idtable(GET_NRCPUS());
        for (ncpu = 0; ncpu < GET_NRCPUS(); ncpu++) {
            /* This code assumes that the bootstrap cpu will be found here */
            if (x86_mp_conf.cpu[ncpu].enabled && !x86_mp_conf.cpu[ncpu].bsp) {
                kprintf("Waking up (%d) AP CPU\n", ncpu);
                ap_ready[0] &= ~0x80;
                setup_ap_stack(ncpu);
                memcpy((prtos_u8_t *)start_eip, (prtos_u8_t *)_PHYS2VIRT(smp_start16), smp_start16_end - smp_start16);
                wake_up_ap(start_eip, x86_mp_conf.cpu[ncpu].id);
                while ((ap_ready[0] & 0x80) != 0x80)
                    ; /*Wait application cpu start*/
            }
        }
    }
}

#else
prtos_s32_t __VBOOT init_smp(void) {
    return 0;
}
#endif /* CONFIG_SMP */
