/* PRTOS ARM architecture core - consolidated */
/* === BEGIN INLINED: arch_arm_arm64_domctl.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * Subarch-specific domctl.c
 *
 * Copyright (c) 2013, Citrix Systems
 */

#include <prtos_types.h>
#include <prtos_lib.h>
#include <prtos_errno.h>
#include <prtos_sched.h>
#include <prtos_hypercall.h>
#include <public_domctl.h>
#include <asm_arm64_sve.h>
#include <asm_cpufeature.h>

static long switch_mode(struct domain *d, enum domain_type type)
{
    struct vcpu *v;

    if ( d == NULL )
        return -EINVAL;
    if ( domain_tot_pages(d) != 0 )
        return -EBUSY;
    if ( d->arch.type == type )
        return 0;

    d->arch.type = type;

    if ( is_64bit_domain(d) )
        for_each_vcpu(d, v)
            vcpu_switch_to_aarch64_mode(v);

    return 0;
}

static long set_address_size(struct domain *d, uint32_t address_size)
{
    switch ( address_size )
    {
    case 32:
        if ( !cpu_has_el1_32 )
            return -EINVAL;
        /* SVE is not supported for 32 bit domain */
        if ( is_sve_domain(d) )
            return -EINVAL;
        return switch_mode(d, DOMAIN_32BIT);
    case 64:
        return switch_mode(d, DOMAIN_64BIT);
    default:
        return -EINVAL;
    }
}

long subarch_do_domctl(struct prtos_domctl *domctl, struct domain *d,
                       PRTOS_GUEST_HANDLE_PARAM(prtos_domctl_t) u_domctl)
{
    switch ( domctl->cmd )
    {
    case PRTOS_DOMCTL_set_address_size:
        return set_address_size(d, domctl->u.address_size.size);

    default:
        return -ENOSYS;
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_arm64_domctl.c === */
/* === BEGIN INLINED: arch_arm_arm64_traps.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
/*
 * prtos/arch/arm/arm64/traps.c
 *
 * ARM AArch64 Specific Trap handlers
 *
 * Copyright (c) 2012 Citrix Systems.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <prtos_prtos_config.h>

#include <prtos_lib.h>
#include <prtos_sched.h>

#include <asm_hsr.h>
#include <asm_system.h>
#include <asm_processor.h>
#include <asm_traps.h>

#include <public_prtos.h>

static const char *handler[]= {
        "Synchronous Abort",
        "IRQ",
        "FIQ",
        "Error"
};

void do_bad_mode(struct cpu_user_regs *regs, int reason)
{
    union hsr hsr = { .bits = regs->hsr };

    printk("Bad mode in %s handler detected\n", handler[reason]);
    printk("ESR=%#"PRIregister":  EC=%"PRIx32", IL=%"PRIx32", ISS=%"PRIx32"\n",
           hsr.bits, hsr.ec, hsr.len, hsr.iss);

    local_irq_disable();
    show_execution_state(regs);
    panic("bad mode\n");
}

void finalize_instr_emulation(const struct instr_details *instr)
{
    struct cpu_user_regs *regs = guest_cpu_user_regs();
    register_t val = 0;
    uint8_t psr_mode = (regs->cpsr & PSR_MODE_MASK);

    /* Currently, we handle only ldr/str post indexing instructions */
    if ( instr->state != INSTR_LDR_STR_POSTINDEXING )
        return;

    /*
     * Handle when rn = SP
     * Refer ArmV8 ARM DDI 0487G.b, Page - D1-2463 "Stack pointer register
     * selection"
     * t = SP_EL0
     * h = SP_ELx
     * and M[3:0] (Page - C5-474 "When exception taken from AArch64 state:")
     */
    if ( instr->rn == 31 )
    {
        switch ( psr_mode )
        {
        case PSR_MODE_EL1h:
            val = regs->sp_el1;
            break;
        case PSR_MODE_EL1t:
        case PSR_MODE_EL0t:
            val = regs->sp_el0;
            break;

        default:
            domain_crash(current->domain);
            return;
        }
    }
    else
        val = get_user_reg(regs, instr->rn);

    val += instr->imm9;

    if ( instr->rn == 31 )
    {
        if ( (regs->cpsr & PSR_MODE_MASK) == PSR_MODE_EL1h )
            regs->sp_el1 = val;
        else
            regs->sp_el0 = val;
    }
    else
        set_user_reg(regs, instr->rn, val);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_arm64_traps.c === */
/* === BEGIN INLINED: arch_arm_cpufeature.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Contains CPU feature definitions
 *
 * Copyright (C) 2015 ARM Ltd.
 */

#include <prtos_bug.h>
#include <prtos_types.h>
#include <prtos_init.h>
#include <prtos_smp.h>
#include <prtos_stop_machine.h>
#include <asm_arm64_sve.h>
#include <asm_cpufeature.h>

DECLARE_BITMAP(cpu_hwcaps, ARM_NCAPS);

struct cpuinfo_arm __read_mostly domain_cpuinfo;

#ifdef CONFIG_ARM_64
static bool has_sb_instruction(const struct arm_cpu_capabilities *entry)
{
    return system_cpuinfo.isa64.sb;
}
#endif

static const struct arm_cpu_capabilities arm_features[] = {
#ifdef CONFIG_ARM_64
    {
        .desc = "Speculation barrier instruction (SB)",
        .capability = ARM_HAS_SB,
        .matches = has_sb_instruction,
    },
#endif
    {},
};

void update_cpu_capabilities(const struct arm_cpu_capabilities *caps,
                             const char *info)
{
    int i;

    for ( i = 0; caps[i].matches; i++ )
    {
        if ( !caps[i].matches(&caps[i]) )
            continue;

        if ( !cpus_have_cap(caps[i].capability) && caps[i].desc )
            printk(PRTOSLOG_INFO "%s: %s\n", info, caps[i].desc);
        cpus_set_cap(caps[i].capability);
    }
}

/*
 * Run through the enabled capabilities and enable() it on all active
 * CPUs.
 */
void __init enable_cpu_capabilities(const struct arm_cpu_capabilities *caps)
{
    for ( ; caps->matches; caps++ )
    {
        if ( !cpus_have_cap(caps->capability) )
            continue;

        if ( caps->enable )
        {
            int ret;

            /*
             * Use stop_machine_run() as it schedules the work allowing
             * us to modify PSTATE, instead of on_each_cpu() which uses
             * an IPI, giving us a PSTATE that disappears when we
             * return.
             */
            ret = stop_machine_run(caps->enable, (void *)caps, NR_CPUS);
            /* stop_machine_run should never fail at this stage of the boot. */
            BUG_ON(ret);
        }
    }
}

void check_local_cpu_features(void)
{
    update_cpu_capabilities(arm_features, "enabled support for");
}

void __init enable_cpu_features(void)
{
    enable_cpu_capabilities(arm_features);
}

/*
 * Run through the enabled capabilities and enable() them on the calling CPU.
 * If enabling of any capability fails the error is returned. After enabling a
 * capability fails the error will be remembered into 'rc' and the remaining
 * capabilities will be enabled. If enabling multiple capabilities fail the
 * error returned by this function represents the error code of the last
 * failure.
 */
int enable_nonboot_cpu_caps(const struct arm_cpu_capabilities *caps)
{
    int rc = 0;

    for ( ; caps->matches; caps++ )
    {
        if ( !cpus_have_cap(caps->capability) )
            continue;

        if ( caps->enable )
        {
            int ret = caps->enable((void *)caps);

            if ( ret )
                rc = ret;
        }
    }

    return rc;
}

void identify_cpu(struct cpuinfo_arm *c)
{
    bool aarch32_el0 = true;

    c->midr.bits = READ_SYSREG(MIDR_EL1);
    c->mpidr.bits = READ_SYSREG(MPIDR_EL1);

#ifdef CONFIG_ARM_64
    c->pfr64.bits[0] = READ_SYSREG(ID_AA64PFR0_EL1);
    c->pfr64.bits[1] = READ_SYSREG(ID_AA64PFR1_EL1);

    c->dbg64.bits[0] = READ_SYSREG(ID_AA64DFR0_EL1);
    c->dbg64.bits[1] = READ_SYSREG(ID_AA64DFR1_EL1);

    c->aux64.bits[0] = READ_SYSREG(ID_AA64AFR0_EL1);
    c->aux64.bits[1] = READ_SYSREG(ID_AA64AFR1_EL1);

    c->mm64.bits[0]  = READ_SYSREG(ID_AA64MMFR0_EL1);
    c->mm64.bits[1]  = READ_SYSREG(ID_AA64MMFR1_EL1);
    c->mm64.bits[2]  = READ_SYSREG(ID_AA64MMFR2_EL1);

    c->isa64.bits[0] = READ_SYSREG(ID_AA64ISAR0_EL1);
    c->isa64.bits[1] = READ_SYSREG(ID_AA64ISAR1_EL1);
    c->isa64.bits[2] = READ_SYSREG(ID_AA64ISAR2_EL1);

    c->zfr64.bits[0] = READ_SYSREG(ID_AA64ZFR0_EL1);

    if ( cpu_has_sve )
        c->zcr64.bits[0] = compute_max_zcr();

    c->dczid.bits[0] = READ_SYSREG(DCZID_EL0);

    c->ctr.bits[0] = READ_SYSREG(CTR_EL0);

    aarch32_el0 = cpu_feature64_has_el0_32(c);
#endif

    if ( aarch32_el0 )
    {
        c->pfr32.bits[0] = READ_SYSREG(ID_PFR0_EL1);
        c->pfr32.bits[1] = READ_SYSREG(ID_PFR1_EL1);
        c->pfr32.bits[2] = READ_SYSREG(ID_PFR2_EL1);

        c->dbg32.bits[0] = READ_SYSREG(ID_DFR0_EL1);
        c->dbg32.bits[1] = READ_SYSREG(ID_DFR1_EL1);

        c->aux32.bits[0] = READ_SYSREG(ID_AFR0_EL1);

        c->mm32.bits[0]  = READ_SYSREG(ID_MMFR0_EL1);
        c->mm32.bits[1]  = READ_SYSREG(ID_MMFR1_EL1);
        c->mm32.bits[2]  = READ_SYSREG(ID_MMFR2_EL1);
        c->mm32.bits[3]  = READ_SYSREG(ID_MMFR3_EL1);
        c->mm32.bits[4]  = READ_SYSREG(ID_MMFR4_EL1);
        c->mm32.bits[5]  = READ_SYSREG(ID_MMFR5_EL1);

        c->isa32.bits[0] = READ_SYSREG(ID_ISAR0_EL1);
        c->isa32.bits[1] = READ_SYSREG(ID_ISAR1_EL1);
        c->isa32.bits[2] = READ_SYSREG(ID_ISAR2_EL1);
        c->isa32.bits[3] = READ_SYSREG(ID_ISAR3_EL1);
        c->isa32.bits[4] = READ_SYSREG(ID_ISAR4_EL1);
        c->isa32.bits[5] = READ_SYSREG(ID_ISAR5_EL1);
        c->isa32.bits[6] = READ_SYSREG(ID_ISAR6_EL1);

        c->mvfr.bits[0] = READ_SYSREG(MVFR0_EL1);
        c->mvfr.bits[1] = READ_SYSREG(MVFR1_EL1);
#ifndef MVFR2_MAYBE_UNDEFINED
        c->mvfr.bits[2] = READ_SYSREG(MVFR2_EL1);
#endif
    }
}

/*
 * This function is creating a cpuinfo structure with values modified to mask
 * all cpu features that should not be published to domains.
 * The created structure is then used to provide ID registers values to domains.
 */
static int __init create_domain_cpuinfo(void)
{
    /* Use the sanitized cpuinfo as initial domain cpuinfo */
    domain_cpuinfo = system_cpuinfo;

#ifdef CONFIG_ARM_64
    /* Hide MPAM support as prtos does not support it */
    domain_cpuinfo.pfr64.mpam = 0;
    domain_cpuinfo.pfr64.mpam_frac = 0;

    /* Hide SVE by default */
    domain_cpuinfo.pfr64.sve = 0;
    domain_cpuinfo.zfr64.bits[0] = 0;

    /* Hide MTE support as PRTOS does not support it */
    domain_cpuinfo.pfr64.mte = 0;

    /* Hide PAC support as PRTOS does not support it */
    domain_cpuinfo.isa64.apa = 0;
    domain_cpuinfo.isa64.api = 0;
    domain_cpuinfo.isa64.gpa = 0;
    domain_cpuinfo.isa64.gpi = 0;
#endif

    /* Hide AMU support */
#ifdef CONFIG_ARM_64
    domain_cpuinfo.pfr64.amu = 0;
#endif
    domain_cpuinfo.pfr32.amu = 0;

    /* Hide RAS support as PRTOS does not support it */
#ifdef CONFIG_ARM_64
    domain_cpuinfo.pfr64.ras = 0;
    domain_cpuinfo.pfr64.ras_frac = 0;
#endif
    domain_cpuinfo.pfr32.ras = 0;
    domain_cpuinfo.pfr32.ras_frac = 0;

    return 0;
}
/*
 * This function needs to be run after all smp are started to have
 * cpuinfo structures for all cores.
 */
__initcall(create_domain_cpuinfo);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_cpufeature.c === */
/* === BEGIN INLINED: arch_arm_domain.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <prtos_prtos_config.h>

#include <prtos_bitops.h>
#include <prtos_errno.h>
#include <prtos_grant_table.h>
#include <prtos_guest_access.h>
#include <prtos_hypercall.h>
#include <prtos_init.h>
#include <prtos_ioreq.h>
#include <prtos_lib.h>
#include <prtos_livepatch.h>
#include <prtos_sched.h>
#include <prtos_softirq.h>
#include <prtos_wait.h>

#include <asm_alternative.h>
#include <asm_arm64_sve.h>
#include <asm_cpuerrata.h>
#include <asm_cpufeature.h>
#include <asm_current.h>
#include <asm_event.h>
#include <asm_gic.h>
#include <asm_guest_atomics.h>
#include <asm_irq.h>
#include <asm_p2m.h>
#include <asm_platform.h>
#include <asm_procinfo.h>
#include <asm_regs.h>
#include <asm_tee_tee.h>
#include <asm_vfp.h>
#include <asm_vgic.h>
#include <asm_vtimer.h>

#include "vpci.h"
#include "vuart.h"

DEFINE_PER_CPU(struct vcpu *, curr_vcpu);

static void do_idle(void) {
    unsigned int cpu = smp_processor_id();

    rcu_idle_enter(cpu);
    /* rcu_idle_enter() can raise TIMER_SOFTIRQ. Process it now. */
    process_pending_softirqs();

    local_irq_disable();
    if (cpu_is_haltable(cpu)) {
        dsb(sy);
        wfi();
    }
    local_irq_enable();

    rcu_idle_exit(cpu);
}

void enable_timer_prtos(void) {
    // Set the timer deadline to 1 second in the future to test Hypervisor Timer
    s_time_t now = NOW();
    this_cpu(timer_deadline) = now + 1000000000;

    if (!reprogram_timer(this_cpu(timer_deadline))) raise_softirq(TIMER_SOFTIRQ);
}

static void noreturn idle_loop(void) {
    unsigned int cpu = smp_processor_id();

    printk("idle_loop: cpu %u\n", cpu);
    {
        extern int prtos_init_secondary_cpu(unsigned int cpu);
        if (prtos_init_secondary_cpu(cpu)) {
            printk("PRTOS Hypervisor is running on CPU %u\n", cpu);
            enable_timer_prtos();
        } else {
            printk("PRTOS: CPU %u has no scheduling work, parking\n", cpu);
            while (1) {
                prtos_u32_t i;
                for (i = 0; i < 1000000; i++) {
                    asm volatile("nop");
                }
            }
        }
    }

    for (;;) {
        if (cpu_is_offline(cpu)) stop_cpu();

        /* Are we here for running vcpu context tasklets, or for idling? */
        if (unlikely(tasklet_work_to_do(cpu))) {
            do_tasklet();
            /* Livepatch work is always kicked off via a tasklet. */
            check_for_livepatch_work();
        }
        /*
         * Test softirqs twice --- first to see if should even try scrubbing
         * and then, after it is done, whether softirqs became pending
         * while we were scrubbing.
         */
        else if (!softirq_pending(cpu) && !scrub_free_pages() && !softirq_pending(cpu))
            do_idle();

        do_softirq();
    }
}

static void ctxt_switch_from(struct vcpu *p) {
    /* When the idle VCPU is running, PRTOS will always stay in hypervisor
     * mode. Therefore we don't need to save the context of an idle VCPU.
     */
    if (is_idle_vcpu(p)) return;

    p2m_save_state(p);

    /* CP 15 */
    p->arch.csselr = READ_SYSREG(CSSELR_EL1);

    /* VFP */
    vfp_save_state(p);

    /* Control Registers */
    p->arch.cpacr = READ_SYSREG(CPACR_EL1);

    p->arch.contextidr = READ_SYSREG(CONTEXTIDR_EL1);
    p->arch.tpidr_el0 = READ_SYSREG(TPIDR_EL0);
    p->arch.tpidrro_el0 = READ_SYSREG(TPIDRRO_EL0);
    p->arch.tpidr_el1 = READ_SYSREG(TPIDR_EL1);

    /* Arch timer */
    p->arch.cntkctl = READ_SYSREG(CNTKCTL_EL1);
    virt_timer_save(p);

    if (is_32bit_domain(p->domain) && cpu_has_thumbee) {
        p->arch.teecr = READ_SYSREG(TEECR32_EL1);
        p->arch.teehbr = READ_SYSREG(TEEHBR32_EL1);
    }

#ifdef CONFIG_ARM_32
    p->arch.joscr = READ_CP32(JOSCR);
    p->arch.jmcr = READ_CP32(JMCR);
#endif

    isb();

    /* MMU */
    p->arch.vbar = READ_SYSREG(VBAR_EL1);
    p->arch.ttbcr = READ_SYSREG(TCR_EL1);
    p->arch.ttbr0 = READ_SYSREG64(TTBR0_EL1);
    p->arch.ttbr1 = READ_SYSREG64(TTBR1_EL1);
    if (is_32bit_domain(p->domain)) p->arch.dacr = READ_SYSREG(DACR32_EL2);
    p->arch.par = read_sysreg_par();
#if defined(CONFIG_ARM_32)
    p->arch.mair0 = READ_CP32(MAIR0);
    p->arch.mair1 = READ_CP32(MAIR1);
    p->arch.amair0 = READ_CP32(AMAIR0);
    p->arch.amair1 = READ_CP32(AMAIR1);
#else
    p->arch.mair = READ_SYSREG64(MAIR_EL1);
    p->arch.amair = READ_SYSREG64(AMAIR_EL1);
#endif

    /* Fault Status */
#if defined(CONFIG_ARM_32)
    p->arch.dfar = READ_CP32(DFAR);
    p->arch.ifar = READ_CP32(IFAR);
    p->arch.dfsr = READ_CP32(DFSR);
#elif defined(CONFIG_ARM_64)
    p->arch.far = READ_SYSREG64(FAR_EL1);
    p->arch.esr = READ_SYSREG64(ESR_EL1);
#endif

    if (is_32bit_domain(p->domain)) p->arch.ifsr = READ_SYSREG(IFSR32_EL2);
    p->arch.afsr0 = READ_SYSREG(AFSR0_EL1);
    p->arch.afsr1 = READ_SYSREG(AFSR1_EL1);

    /* XXX MPU */

    /* VGIC */
    gic_save_state(p);

    isb();
}

static void ctxt_switch_to(struct vcpu *n) {
    register_t vpidr;

    /* When the idle VCPU is running, PRTOS will always stay in hypervisor
     * mode. Therefore we don't need to restore the context of an idle VCPU.
     */
    if (is_idle_vcpu(n)) return;

    vpidr = READ_SYSREG(MIDR_EL1);
    WRITE_SYSREG(vpidr, VPIDR_EL2);
    WRITE_SYSREG(n->arch.vmpidr, VMPIDR_EL2);

    /* VGIC */
    gic_restore_state(n);

    /* XXX MPU */

    /* Fault Status */
#if defined(CONFIG_ARM_32)
    WRITE_CP32(n->arch.dfar, DFAR);
    WRITE_CP32(n->arch.ifar, IFAR);
    WRITE_CP32(n->arch.dfsr, DFSR);
#elif defined(CONFIG_ARM_64)
    WRITE_SYSREG64(n->arch.far, FAR_EL1);
    WRITE_SYSREG64(n->arch.esr, ESR_EL1);
#endif

    if (is_32bit_domain(n->domain)) WRITE_SYSREG(n->arch.ifsr, IFSR32_EL2);
    WRITE_SYSREG(n->arch.afsr0, AFSR0_EL1);
    WRITE_SYSREG(n->arch.afsr1, AFSR1_EL1);

    /* MMU */
    WRITE_SYSREG(n->arch.vbar, VBAR_EL1);
    WRITE_SYSREG(n->arch.ttbcr, TCR_EL1);
    WRITE_SYSREG64(n->arch.ttbr0, TTBR0_EL1);
    WRITE_SYSREG64(n->arch.ttbr1, TTBR1_EL1);

    /*
     * Erratum #852523 (Cortex-A57) or erratum #853709 (Cortex-A72):
     * DACR32_EL2 must be restored before one of the
     * following sysregs: SCTLR_EL1, TCR_EL1, TTBR0_EL1, TTBR1_EL1 or
     * CONTEXTIDR_EL1.
     */
    if (is_32bit_domain(n->domain)) WRITE_SYSREG(n->arch.dacr, DACR32_EL2);
    WRITE_SYSREG64(n->arch.par, PAR_EL1);
#if defined(CONFIG_ARM_32)
    WRITE_CP32(n->arch.mair0, MAIR0);
    WRITE_CP32(n->arch.mair1, MAIR1);
    WRITE_CP32(n->arch.amair0, AMAIR0);
    WRITE_CP32(n->arch.amair1, AMAIR1);
#elif defined(CONFIG_ARM_64)
    WRITE_SYSREG64(n->arch.mair, MAIR_EL1);
    WRITE_SYSREG64(n->arch.amair, AMAIR_EL1);
#endif
    isb();

    /*
     * ARM64_WORKAROUND_AT_SPECULATE: The P2M should be restored after
     * the stage-1 MMU sysregs have been restored.
     */
    p2m_restore_state(n);

    /* Control Registers */
    WRITE_SYSREG(n->arch.cpacr, CPACR_EL1);

    /*
     * This write to sysreg CONTEXTIDR_EL1 ensures we don't hit erratum
     * #852523 (Cortex-A57) or #853709 (Cortex-A72).
     * I.e DACR32_EL2 is not correctly synchronized.
     */
    WRITE_SYSREG(n->arch.contextidr, CONTEXTIDR_EL1);
    WRITE_SYSREG(n->arch.tpidr_el0, TPIDR_EL0);
    WRITE_SYSREG(n->arch.tpidrro_el0, TPIDRRO_EL0);
    WRITE_SYSREG(n->arch.tpidr_el1, TPIDR_EL1);

    if (is_32bit_domain(n->domain) && cpu_has_thumbee) {
        WRITE_SYSREG(n->arch.teecr, TEECR32_EL1);
        WRITE_SYSREG(n->arch.teehbr, TEEHBR32_EL1);
    }

#ifdef CONFIG_ARM_32
    WRITE_CP32(n->arch.joscr, JOSCR);
    WRITE_CP32(n->arch.jmcr, JMCR);
#endif

    /*
     * CPTR_EL2 needs to be written before calling vfp_restore_state, a
     * synchronization instruction is expected after the write (isb)
     */
    WRITE_SYSREG(n->arch.cptr_el2, CPTR_EL2);
    isb();

    /* VFP - call vfp_restore_state after writing on CPTR_EL2 + isb */
    vfp_restore_state(n);

    /* CP 15 */
    WRITE_SYSREG(n->arch.csselr, CSSELR_EL1);

    isb();

    /* This is could trigger an hardware interrupt from the virtual
     * timer. The interrupt needs to be injected into the guest. */
    WRITE_SYSREG(n->arch.cntkctl, CNTKCTL_EL1);
    virt_timer_restore(n);

    WRITE_SYSREG(n->arch.mdcr_el2, MDCR_EL2);
}

static void schedule_tail(struct vcpu *prev) {
    ASSERT(prev != current);

    ctxt_switch_from(prev);

    ctxt_switch_to(current);

    local_irq_enable();

    sched_context_switched(prev, current);

    update_runstate_area(current);

    /* Ensure that the vcpu has an up-to-date time base. */
    update_vcpu_system_time(current);
}

extern void noreturn return_to_new_vcpu32(void);
extern void noreturn return_to_new_vcpu64(void);

static void continue_new_vcpu(struct vcpu *prev) {
    current->arch.actlr = READ_SYSREG(ACTLR_EL1);
    processor_vcpu_initialise(current);

    schedule_tail(prev);

    if (is_idle_vcpu(current))
        reset_stack_and_jump(idle_loop);
    else if (is_32bit_domain(current->domain))
        /* check_wakeup_from_wait(); */
        reset_stack_and_jump(return_to_new_vcpu32);
    else
        /* check_wakeup_from_wait(); */
        reset_stack_and_jump(return_to_new_vcpu64);
}

void context_switch(struct vcpu *prev, struct vcpu *next) {
    ASSERT(local_irq_is_enabled());
    ASSERT(prev != next);
    ASSERT(!vcpu_cpu_dirty(next));

    update_runstate_area(prev);

    local_irq_disable();

    set_current(next);

    prev = __context_switch(prev, next);

    schedule_tail(prev);
}

void continue_running(struct vcpu *same) {
    /* Nothing to do */
}

void sync_local_execstate(void) {
    /* Nothing to do -- no lazy switching */
}

void sync_vcpu_execstate(struct vcpu *v) {
    /*
     * We don't support lazy switching.
     *
     * However the context may have been saved from a remote pCPU so we
     * need a barrier to ensure it is observed before continuing.
     *
     * Per vcpu_context_saved(), the context can be observed when
     * v->is_running is false (the caller should check it before calling
     * this function).
     *
     * Note this is a full barrier to also prevent update of the context
     * to happen before it was observed.
     */
    smp_mb();
}

#define NEXT_ARG(fmt, args)                                         \
    ({                                                              \
        unsigned long __arg;                                        \
        switch (*(fmt)++) {                                         \
            case 'i':                                               \
                __arg = (unsigned long)va_arg(args, unsigned int);  \
                break;                                              \
            case 'l':                                               \
                __arg = (unsigned long)va_arg(args, unsigned long); \
                break;                                              \
            case 'h':                                               \
                __arg = (unsigned long)va_arg(args, void *);        \
                break;                                              \
            default:                                                \
                goto bad_fmt;                                       \
        }                                                           \
        __arg;                                                      \
    })

unsigned long hypercall_create_continuation(unsigned int op, const char *format, ...) {
    struct mc_state *mcs = &current->mc_state;
    struct cpu_user_regs *regs;
    const char *p = format;
    unsigned long arg, rc;
    unsigned int i;
    /* SAF-4-safe allowed variadic function */
    va_list args;

    current->hcall_preempted = true;

    va_start(args, format);

    if (mcs->flags & MCSF_in_multicall) {
        for (i = 0; *p != '\0'; i++) mcs->call.args[i] = NEXT_ARG(p, args);

        /* Return value gets written back to mcs->call.result */
        rc = mcs->call.result;
    } else {
        regs = guest_cpu_user_regs();

#ifdef CONFIG_ARM_64
        if (!is_32bit_domain(current->domain)) {
            regs->x16 = op;

            for (i = 0; *p != '\0'; i++) {
                arg = NEXT_ARG(p, args);

                switch (i) {
                    case 0:
                        regs->x0 = arg;
                        break;
                    case 1:
                        regs->x1 = arg;
                        break;
                    case 2:
                        regs->x2 = arg;
                        break;
                    case 3:
                        regs->x3 = arg;
                        break;
                    case 4:
                        regs->x4 = arg;
                        break;
                    case 5:
                        regs->x5 = arg;
                        break;
                }
            }

            /* Return value gets written back to x0 */
            rc = regs->x0;
        } else
#endif
        {
            regs->r12 = op;

            for (i = 0; *p != '\0'; i++) {
                arg = NEXT_ARG(p, args);

                switch (i) {
                    case 0:
                        regs->r0 = arg;
                        break;
                    case 1:
                        regs->r1 = arg;
                        break;
                    case 2:
                        regs->r2 = arg;
                        break;
                    case 3:
                        regs->r3 = arg;
                        break;
                    case 4:
                        regs->r4 = arg;
                        break;
                    case 5:
                        regs->r5 = arg;
                        break;
                }
            }

            /* Return value gets written back to r0 */
            rc = regs->r0;
        }
    }

    va_end(args);

    return rc;

bad_fmt:
    va_end(args);
    gprintk(PRTOSLOG_ERR, "Bad hypercall continuation format '%c'\n", *p);
    ASSERT_UNREACHABLE();
    domain_crash(current->domain);
    return 0;
}

#undef NEXT_ARG

void startup_cpu_idle_loop(void) {
    struct vcpu *v = current;

    ASSERT(is_idle_vcpu(v));
    /* TODO
       cpumask_set_cpu(v->processor, v->domain->dirty_cpumask);
       v->dirty_cpu = v->processor;
    */

    reset_stack_and_jump(idle_loop);
}


void free_domain_struct(struct domain *d) {
    free_prtosheap_page(d);
}

void dump_pageframe_info(struct domain *d) {}

/*
 * The new VGIC has a bigger per-IRQ structure, so we need more than one
 * page on ARM64. Cowardly increase the limit in this case.
 */
#if defined(CONFIG_NEW_VGIC) && defined(CONFIG_ARM_64)
#define MAX_PAGES_PER_VCPU 2
#else
#define MAX_PAGES_PER_VCPU 1
#endif

static struct vcpu local_vcpu_prtos[NR_CPUS];
static unsigned int local_vcpu_prtos_index = 0;

struct vcpu *alloc_vcpu_struct(const struct domain *d) {
    struct vcpu *v;

    // BUILD_BUG_ON(sizeof(*v) > MAX_PAGES_PER_VCPU * PAGE_SIZE);
    // v = alloc_prtosheap_pages(get_order_from_bytes(sizeof(*v)), 0);
    // if ( v != NULL )
    // {
    //     unsigned int i;

    //     for ( i = 0; i < DIV_ROUND_UP(sizeof(*v), PAGE_SIZE); i++ )
    //         clear_page((void *)v + i * PAGE_SIZE);
    // }

    v = &local_vcpu_prtos[local_vcpu_prtos_index++];
    if (local_vcpu_prtos_index > NR_CPUS) {
        printk("######### local_vcpu_prtos_index overflow\n");
        return NULL;
    }

    return v;
}

void free_vcpu_struct(struct vcpu *v) {
    // free_prtosheap_pages(v, get_order_from_bytes(sizeof(*v)));
}

static char stack[STACK_SIZE * 8 * NR_CPUS] __aligned(PAGE_SIZE);
static unsigned int stack_index = 0;
int arch_vcpu_create(struct vcpu *v) {
    int rc = 0;

    BUILD_BUG_ON(sizeof(struct cpu_info) > STACK_SIZE);

    // v->arch.stack = alloc_prtosheap_pages(STACK_ORDER, MEMF_node(vcpu_to_node(v)));
    // if ( v->arch.stack == NULL )
    //     return -ENOMEM;
    v->arch.stack = &stack[STACK_SIZE * 8 * stack_index++];
    if (stack_index > NR_CPUS) {
        printk("######### stack_index overflow\n");
        return -ENOMEM;
    }
    memset(v->arch.stack, 0, STACK_SIZE * 8);
    v->arch.cpu_info = (struct cpu_info *)(v->arch.stack + STACK_SIZE - sizeof(struct cpu_info));
    memset(v->arch.cpu_info, 0, sizeof(*v->arch.cpu_info));

    v->arch.saved_context.sp = (register_t)v->arch.cpu_info;
    v->arch.saved_context.pc = (register_t)continue_new_vcpu;

    /* Idle VCPUs don't need the rest of this setup */
    if (is_idle_vcpu(v)) return rc;

    v->arch.sctlr = SCTLR_GUEST_INIT;

    v->arch.vmpidr = MPIDR_SMP | vcpuid_to_vaffinity(v->vcpu_id);

    v->arch.cptr_el2 = get_default_cptr_flags();
    if (is_sve_domain(v->domain)) {
        if ((rc = sve_context_init(v)) != 0) goto fail;
        v->arch.cptr_el2 &= ~HCPTR_CP(8);
    }

    v->arch.hcr_el2 = get_default_hcr_flags();

    v->arch.mdcr_el2 = HDCR_TDRA | HDCR_TDOSA | HDCR_TDA;
    if (!(v->domain->options & PRTOS_DOMCTL_CDF_vpmu)) v->arch.mdcr_el2 |= HDCR_TPM | HDCR_TPMCR;

    if ((rc = vcpu_vgic_init(v)) != 0) goto fail;

    if ((rc = vcpu_vtimer_init(v)) != 0) goto fail;

    /*
     * The workaround 2 (i.e SSBD mitigation) is enabled by default if
     * supported.
     */
    if (get_ssbd_state() == ARM_SSBD_RUNTIME) v->arch.cpu_info->flags |= CPUINFO_WORKAROUND_2_FLAG;

    return rc;

fail:
    arch_vcpu_destroy(v);
    return rc;
}

void arch_vcpu_destroy(struct vcpu *v) {
    if (is_sve_domain(v->domain)) sve_context_free(v);
    vcpu_timer_destroy(v);
    vcpu_vgic_free(v);
    free_prtosheap_pages(v->arch.stack, STACK_ORDER);
}

void vcpu_switch_to_aarch64_mode(struct vcpu *v) {
    v->arch.hcr_el2 |= HCR_RW;
}

int arch_sanitise_domain_config(struct prtos_domctl_createdomain *config) {
    unsigned int max_vcpus;
    unsigned int flags_required = (PRTOS_DOMCTL_CDF_hvm | PRTOS_DOMCTL_CDF_hap);
    unsigned int flags_optional = (PRTOS_DOMCTL_CDF_iommu | PRTOS_DOMCTL_CDF_vpmu);
    unsigned int sve_vl_bits = sve_decode_vl(config->arch.sve_vl);

    if ((config->flags & ~flags_optional) != flags_required) {
        dprintk(PRTOSLOG_INFO, "Unsupported configuration %#x\n", config->flags);
        return -EINVAL;
    }

    /* Check feature flags */
    if (sve_vl_bits > 0) {
        unsigned int zcr_max_bits = get_sys_vl_len();

        if (!zcr_max_bits) {
            dprintk(PRTOSLOG_INFO, "SVE is unsupported on this machine.\n");
            return -EINVAL;
        }

        if (sve_vl_bits > zcr_max_bits) {
            dprintk(PRTOSLOG_INFO, "Requested SVE vector length (%u) > supported length (%u)\n", sve_vl_bits, zcr_max_bits);
            return -EINVAL;
        }
    }

    /* The P2M table must always be shared between the CPU and the IOMMU */
    if (config->iommu_opts & PRTOS_DOMCTL_IOMMU_no_sharept) {
        dprintk(PRTOSLOG_INFO, "Unsupported iommu option: PRTOS_DOMCTL_IOMMU_no_sharept\n");
        return -EINVAL;
    }

    /* Fill in the native GIC version, passed back to the toolstack. */
    if (config->arch.gic_version == PRTOS_DOMCTL_CONFIG_GIC_NATIVE) {
        switch (gic_hw_version()) {
            case GIC_V2:
                config->arch.gic_version = PRTOS_DOMCTL_CONFIG_GIC_V2;
                break;

            case GIC_V3:
                config->arch.gic_version = PRTOS_DOMCTL_CONFIG_GIC_V3;
                break;

            default:
                ASSERT_UNREACHABLE();
                return -EINVAL;
        }
    }

    /* max_vcpus depends on the GIC version, and PRTOS's compiled limit. */
    max_vcpus = min(vgic_max_vcpus(config->arch.gic_version), MAX_VIRT_CPUS);

    if (max_vcpus == 0) {
        dprintk(PRTOSLOG_INFO, "Unsupported GIC version\n");
        return -EINVAL;
    }

    if (config->max_vcpus > max_vcpus) {
        dprintk(PRTOSLOG_INFO, "Requested vCPUs (%u) exceeds max (%u)\n", config->max_vcpus, max_vcpus);
        return -EINVAL;
    }

    if (config->arch.tee_type != PRTOS_DOMCTL_CONFIG_TEE_NONE && config->arch.tee_type != tee_get_type()) {
        dprintk(PRTOSLOG_INFO, "Unsupported TEE type\n");
        return -EINVAL;
    }

    if (config->altp2m_opts) {
        dprintk(PRTOSLOG_INFO, "Altp2m not supported\n");
        return -EINVAL;
    }

    return 0;
}

int arch_domain_create(struct domain *d, struct prtos_domctl_createdomain *config, unsigned int flags) {
    unsigned int count = 0;
    int rc;

    BUILD_BUG_ON(GUEST_MAX_VCPUS < MAX_VIRT_CPUS);

    /* Idle domains do not need this setup */
    if (is_idle_domain(d)) return 0;

    ASSERT(config != NULL);

#ifdef CONFIG_IOREQ_SERVER
    ioreq_domain_init(d);
#endif

    /* p2m_init relies on some value initialized by the IOMMU subsystem */
    if ((rc = iommu_domain_init(d, config->iommu_opts)) != 0) goto fail;

    if ((rc = p2m_init(d)) != 0) goto fail;

    rc = -ENOMEM;
    if ((d->shared_info = alloc_prtosheap_pages(0, 0)) == NULL) goto fail;

    clear_page(d->shared_info);
    share_prtos_page_with_guest(virt_to_page(d->shared_info), d, SHARE_rw);

    switch (config->arch.gic_version) {
        case PRTOS_DOMCTL_CONFIG_GIC_V2:
            d->arch.vgic.version = GIC_V2;
            break;

        case PRTOS_DOMCTL_CONFIG_GIC_V3:
            d->arch.vgic.version = GIC_V3;
            break;

        default:
            BUG();
    }

    if ((rc = domain_vgic_register(d, &count)) != 0) goto fail;

    count += domain_vpci_get_num_mmio_handlers(d);

    if ((rc = domain_io_init(d, count + MAX_IO_HANDLER)) != 0) goto fail;

    if ((rc = domain_vgic_init(d, config->arch.nr_spis)) != 0) goto fail;

    if ((rc = domain_vtimer_init(d, &config->arch)) != 0) goto fail;

    if ((rc = tee_domain_init(d, config->arch.tee_type)) != 0) goto fail;

    update_domain_wallclock_time(d);

    /*
     * The hardware domain will get a PPI later in
     * arch/arm/domain_build.c  depending on the
     * interrupt map of the hardware.
     */
    if (!is_hardware_domain(d)) {
        d->arch.evtchn_irq = GUEST_EVTCHN_PPI;
        /* At this stage vgic_reserve_virq should never fail */
        if (!vgic_reserve_virq(d, GUEST_EVTCHN_PPI)) BUG();
    }

    /*
     * Virtual UART provides console output for all partitions.
     * Each partition's UART writes are trapped and multiplexed
     * to the physical serial port by the hypervisor.
     */
    if ((rc = domain_vuart_init(d))) goto fail;

    if ((rc = domain_vpci_init(d)) != 0) goto fail;

#ifdef CONFIG_ARM64_SVE
    /* Copy the encoded vector length sve_vl from the domain configuration */
    d->arch.sve_vl = config->arch.sve_vl;
#endif

    return 0;

fail:
    d->is_dying = DOMDYING_dead;
    arch_domain_destroy(d);

    return rc;
}

int arch_domain_teardown(struct domain *d) {
    int ret = 0;

    BUG_ON(!d->is_dying);

    /* See domain_teardown() for an explanation of all of this magic. */
    switch (d->teardown.arch_val) {
#define PROGRESS(x)                  \
    d->teardown.arch_val = PROG_##x; \
    fallthrough;                     \
    case PROG_##x

        enum {
            PROG_none,
            PROG_tee,
            PROG_done,
        };

        case PROG_none:
            BUILD_BUG_ON(PROG_none != 0);

            PROGRESS(tee) : ret = tee_domain_teardown(d);
            if (ret) return ret;

            PROGRESS(done) : break;

#undef PROGRESS

        default:
            BUG();
    }

    return 0;
}

void arch_domain_destroy(struct domain *d) {
    tee_free_domain_ctx(d);
    /* IOMMU page table is shared with P2M, always call
     * iommu_domain_destroy() before p2m_final_teardown().
     */
    iommu_domain_destroy(d);
    p2m_final_teardown(d);
    domain_vgic_free(d);
    domain_vuart_free(d);
    free_prtosheap_page(d->shared_info);
#ifdef CONFIG_ACPI
    free_prtosheap_pages(d->arch.efi_acpi_table, get_order_from_bytes(d->arch.efi_acpi_len));
#endif
    domain_io_free(d);
}

void arch_domain_shutdown(struct domain *d) {}

void arch_domain_pause(struct domain *d) {}

void arch_domain_unpause(struct domain *d) {}

int arch_domain_soft_reset(struct domain *d) {
    return -ENOSYS;
}

void arch_domain_creation_finished(struct domain *d) {
    p2m_domain_creation_finished(d);
}

static int is_guest_pv32_psr(uint32_t psr) {
    switch (psr & PSR_MODE_MASK) {
        case PSR_MODE_USR:
        case PSR_MODE_FIQ:
        case PSR_MODE_IRQ:
        case PSR_MODE_SVC:
        case PSR_MODE_ABT:
        case PSR_MODE_UND:
        case PSR_MODE_SYS:
            return 1;
        case PSR_MODE_MON:
        case PSR_MODE_HYP:
        default:
            return 0;
    }
}

#ifdef CONFIG_ARM_64
static int is_guest_pv64_psr(uint64_t psr) {
    if (psr & PSR_MODE_BIT) return 0;

    switch (psr & PSR_MODE_MASK) {
        case PSR_MODE_EL1h:
        case PSR_MODE_EL1t:
        case PSR_MODE_EL0t:
            return 1;
        case PSR_MODE_EL3h:
        case PSR_MODE_EL3t:
        case PSR_MODE_EL2h:
        case PSR_MODE_EL2t:
        default:
            return 0;
    }
}
#endif

/*
 * Initialise vCPU state. The context may be supplied by an external entity, so
 * we need to validate it.
 */
int arch_set_info_guest(struct vcpu *v, vcpu_guest_context_u c) {
    struct vcpu_guest_context *ctxt = c.nat;
    struct vcpu_guest_core_regs *regs = &c.nat->user_regs;

    if (is_32bit_domain(v->domain)) {
        if (!is_guest_pv32_psr(regs->cpsr)) return -EINVAL;

        if (regs->spsr_svc && !is_guest_pv32_psr(regs->spsr_svc)) return -EINVAL;
        if (regs->spsr_abt && !is_guest_pv32_psr(regs->spsr_abt)) return -EINVAL;
        if (regs->spsr_und && !is_guest_pv32_psr(regs->spsr_und)) return -EINVAL;
        if (regs->spsr_irq && !is_guest_pv32_psr(regs->spsr_irq)) return -EINVAL;
        if (regs->spsr_fiq && !is_guest_pv32_psr(regs->spsr_fiq)) return -EINVAL;
    }
#ifdef CONFIG_ARM_64
    else {
        if (!is_guest_pv64_psr(regs->cpsr)) return -EINVAL;

        if (regs->spsr_el1 && !is_guest_pv64_psr(regs->spsr_el1)) return -EINVAL;
    }
#endif

    vcpu_regs_user_to_hyp(v, regs);

    v->arch.sctlr = ctxt->sctlr;
    v->arch.ttbr0 = ctxt->ttbr0;
    v->arch.ttbr1 = ctxt->ttbr1;
    v->arch.ttbcr = ctxt->ttbcr;

    v->is_initialised = 1;

    if (ctxt->flags & VGCF_online)
        clear_bit(_VPF_down, &v->pause_flags);
    else
        set_bit(_VPF_down, &v->pause_flags);

    return 0;
}

int arch_initialise_vcpu(struct vcpu *v, PRTOS_GUEST_HANDLE_PARAM(void) arg) {
    ASSERT_UNREACHABLE();
    return -EOPNOTSUPP;
}

int arch_vcpu_reset(struct vcpu *v) {
    vcpu_end_shutdown_deferral(v);
    return 0;
}

static int relinquish_memory(struct domain *d, struct page_list_head *list) {
    struct page_info *page, *tmp;
    int ret = 0;

    /* Use a recursive lock, as we may enter 'free_domheap_page'. */
    rspin_lock(&d->page_alloc_lock);

    page_list_for_each_safe(page, tmp, list) {
        /* Grab a reference to the page so it won't disappear from under us. */
        if (unlikely(!get_page(page, d)))
            /*
             * Couldn't get a reference -- someone is freeing this page and
             * has already committed to doing so, so no more to do here.
             *
             * Note that the page must be left on the list, a list_del
             * here will clash with the list_del done by the other
             * party in the race and corrupt the list head.
             */
            continue;

        put_page_alloc_ref(page);
        put_page(page);

        if (hypercall_preempt_check()) {
            ret = -ERESTART;
            goto out;
        }
    }

out:
    rspin_unlock(&d->page_alloc_lock);
    return ret;
}

/*
 * Record the current progress. Subsequent hypercall continuations will
 * logically restart work from this point.
 *
 * PROGRESS() markers must not be in the middle of loops. The loop
 * variable isn't preserved accross a continuation.
 *
 * To avoid redundant work, there should be a marker before each
 * function which may return -ERESTART.
 */
enum {
    PROG_pci = 1,
    PROG_tee,
    PROG_prtos,
    PROG_page,
    PROG_mapping,
    PROG_p2m_root,
    PROG_p2m,
    PROG_p2m_pool,
    PROG_done,
};

#define PROGRESS(x)              \
    d->arch.rel_priv = PROG_##x; \
    /* Fallthrough */            \
    case PROG_##x

int domain_relinquish_resources(struct domain *d) {
    int ret = 0;

    /*
     * This hypercall can take minutes of wallclock time to complete.  This
     * logic implements a co-routine, stashing state in struct domain across
     * hypercall continuation boundaries.
     */
    switch (d->arch.rel_priv) {
        case 0:
            ret = iommu_release_dt_devices(d);
            if (ret) return ret;

            /*
             * Release the resources allocated for vpl011 which were
             * allocated via a DOMCTL call PRTOS_DOMCTL_vuart_op.
             */
            domain_vpl011_deinit(d);

#ifdef CONFIG_IOREQ_SERVER
            ioreq_server_destroy_all(d);
#endif
#ifdef CONFIG_HAS_PCI
            PROGRESS(pci) : ret = pci_release_devices(d);
            if (ret) return ret;
#endif

            PROGRESS(tee) : ret = tee_relinquish_resources(d);
            if (ret) return ret;

            PROGRESS(prtos) : ret = relinquish_memory(d, &d->prtospage_list);
            if (ret) return ret;

            PROGRESS(page) : ret = relinquish_memory(d, &d->page_list);
            if (ret) return ret;

            PROGRESS(mapping) : ret = relinquish_p2m_mapping(d);
            if (ret) return ret;

            PROGRESS(p2m_root)
                : /*
                   * We are about to free the intermediate page-tables, so clear the
                   * root to prevent any walk to use them.
                   */
                  p2m_clear_root_pages(&d->arch.p2m);

            PROGRESS(p2m) : ret = p2m_teardown(d);
            if (ret) return ret;

            PROGRESS(p2m_pool) : ret = p2m_teardown_allocation(d);
            if (ret) return ret;

            PROGRESS(done) : break;

        default:
            BUG();
    }

    return 0;
}

#undef PROGRESS

void arch_dump_domain_info(struct domain *d) {
    p2m_dump_info(d);
}

long do_vcpu_op(int cmd, unsigned int vcpuid, PRTOS_GUEST_HANDLE_PARAM(void) arg) {
    struct domain *d = current->domain;
    struct vcpu *v;

    if ((v = domain_vcpu(d, vcpuid)) == NULL) return -ENOENT;

    switch (cmd) {
        case VCPUOP_register_vcpu_info:
        case VCPUOP_register_runstate_memory_area:
            return common_vcpu_op(cmd, v, arg);
        default:
            return -EINVAL;
    }
}

void arch_dump_vcpu_info(struct vcpu *v) {
    gic_dump_info(v);
    gic_dump_vgic_info(v);
}

void vcpu_mark_events_pending(struct vcpu *v) {
    bool already_pending = guest_test_and_set_bit(v->domain, 0, (unsigned long *)&vcpu_info(v, evtchn_upcall_pending));

    if (already_pending) return;

    vgic_inject_irq(v->domain, v, v->domain->arch.evtchn_irq, true);
}

void vcpu_update_evtchn_irq(struct vcpu *v) {
    bool pending = vcpu_info(v, evtchn_upcall_pending);

    vgic_inject_irq(v->domain, v, v->domain->arch.evtchn_irq, pending);
}

/* The ARM spec declares that even if local irqs are masked in
 * the CPSR register, an irq should wake up a cpu from WFI anyway.
 * For this reason we need to check for irqs that need delivery,
 * ignoring the CPSR register, *after* calling SCHEDOP_block to
 * avoid races with vgic_inject_irq.
 */
void vcpu_block_unless_event_pending(struct vcpu *v) {
    vcpu_block();
    if (local_events_need_delivery_nomask()) vcpu_unblock(current);
}

void vcpu_kick(struct vcpu *v) {
    bool running = v->is_running;

    vcpu_unblock(v);
    if (running && v != current) {
        perfc_incr(vcpu_kick);
        smp_send_event_check_mask(cpumask_of(v->processor));
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_domain.c === */
/* === BEGIN INLINED: arch_arm_processor.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/processor.c
 *
 * Helpers to execute processor specific code.
 *
 * Julien Grall <julien.grall@linaro.org>
 * Copyright (C) 2014 Linaro Limited.
 */
#include <asm_procinfo.h>

static DEFINE_PER_CPU(struct processor *, processor);

void processor_setup(void)
{
    const struct proc_info_list *procinfo;

    procinfo = lookup_processor_type();
    if ( !procinfo )
        return;

    this_cpu(processor) = procinfo->processor;
}

void processor_vcpu_initialise(struct vcpu *v)
{
    if ( !this_cpu(processor) || !this_cpu(processor)->vcpu_initialise )
        return;

    this_cpu(processor)->vcpu_initialise(v);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_processor.c === */
/* === BEGIN INLINED: arch_arm_setup.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/setup.c
 *
 * Early bringup code for an ARMv7-A with virt extensions.
 *
 * Tim Deegan <tim@xen.org>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <prtos_compile.h>
#include <prtos_device_tree.h>
#include <prtos_domain_page.h>
#include <prtos_grant_table.h>
#include <prtos_types.h>
#include <prtos_string.h>
#include <prtos_serial.h>
#include <prtos_sched.h>
#include <prtos_console.h>
#include <prtos_err.h>
#include <prtos_init.h>
#include <prtos_irq.h>
#include <prtos_mm.h>
#include <prtos_param.h>
#include <prtos_softirq.h>
#include <prtos_keyhandler.h>
#include <prtos_cpu.h>
#include <prtos_pfn.h>
#include <prtos_virtual_region.h>
#include <prtos_vmap.h>
#include <prtos_trace.h>
#include <prtos_libfdt_libfdt-prtos.h>
#include <prtos_acpi.h>
#include <prtos_warning.h>
#include <prtos_hypercall.h>
#include <asm_alternative.h>
#include <asm_dom0less-build.h>
#include <asm_page.h>
#include <asm_static-evtchn.h>
#include <asm_current.h>
#include <asm_setup.h>
#include <asm_gic.h>
#include <asm_cpuerrata.h>
#include <asm_cpufeature.h>
#include <asm_platform.h>
#include <asm_procinfo.h>
#include <asm_setup.h>
#include <prtos_xsm_xsm.h>
#include <asm_acpi.h>

struct bootinfo __initdata bootinfo = BOOTINFO_INIT;

/*
 * Sanitized version of cpuinfo containing only features available on all
 * cores (only on arm64 as there is no sanitization support on arm32).
 */
struct cpuinfo_arm __read_mostly system_cpuinfo;

#ifdef CONFIG_ACPI
bool __read_mostly acpi_disabled;
#endif

domid_t __read_mostly max_init_domid;

static __used void init_done(void) {
    printk("init_done start ...\n");
    int rc;

    /* Must be done past setting system_state. */
    unregister_init_virtual_region();

    free_init_memory();

    /*
     * We have finished booting. Mark the section .data.ro_after_init
     * read-only.
     */
    rc = modify_prtos_mappings((unsigned long)&__ro_after_init_start, (unsigned long)&__ro_after_init_end, PAGE_HYPERVISOR_RO);
    if (rc) panic("Unable to mark the .data.ro_after_init section read-only (rc = %d)\n", rc);

    startup_cpu_idle_loop();
}

void __init init_idle_domain_prtos(void) {
    scheduler_init();
    set_current(idle_vcpu[0]);
    /* TODO: setup_idle_pagetable(); */

    /*
     * The idle vCPU skips vcpu_vgic_init() in arch_vcpu_create().
     * However, leave_hypervisor_to_guest() unconditionally calls
     * vgic_sync_to_lrs() which walks the lr_pending and inflight_irqs
     * linked lists.  Without initialization those lists have next=NULL
     * instead of next=&list_head (empty sentinel), causing a NULL
     * pointer dereference on the first return from EL2 to EL1.
     *
     * Initialize them here so vgic_sync_to_lrs() sees an empty list
     * and returns without iterating.
     */
    INIT_LIST_HEAD(&idle_vcpu[0]->arch.vgic.inflight_irqs);
    INIT_LIST_HEAD(&idle_vcpu[0]->arch.vgic.lr_pending);
    spin_lock_init(&idle_vcpu[0]->arch.vgic.lock);
}

static const char *__initdata processor_implementers[] = {
    ['A'] = "ARM Limited",
    ['B'] = "Broadcom Corporation",
    ['C'] = "Cavium Inc.",
    ['D'] = "Digital Equipment Corp",
    ['M'] = "Motorola, Freescale Semiconductor Inc.",
    ['P'] = "Applied Micro",
    ['Q'] = "Qualcomm Inc.",
    ['V'] = "Marvell Semiconductor Inc.",
    ['i'] = "Intel Corporation",
};

void __init processor_id(void) {
    const char *implementer = "Unknown";
    struct cpuinfo_arm *c = &system_cpuinfo;

    identify_cpu(c);
    current_cpu_data = *c;

    if (c->midr.implementer < ARRAY_SIZE(processor_implementers) && processor_implementers[c->midr.implementer])
        implementer = processor_implementers[c->midr.implementer];

    if (c->midr.architecture != 0xf) printk("Huh, cpu architecture %x, expected 0xf (defined by cpuid)\n", c->midr.architecture);

    printk("Processor: %" PRIregister ": \"%s\", variant: 0x%x, part 0x%03x,"
           "rev 0x%x\n",
           c->midr.bits, implementer, c->midr.variant, c->midr.part_number, c->midr.revision);

#if defined(CONFIG_ARM_64)
    printk("64-bit Execution:\n");
    printk("  Processor Features: %016" PRIx64 " %016" PRIx64 "\n", system_cpuinfo.pfr64.bits[0], system_cpuinfo.pfr64.bits[1]);
    printk("    Exception Levels: EL3:%s EL2:%s EL1:%s EL0:%s\n",
           cpu_has_el3_32   ? "64+32"
           : cpu_has_el3_64 ? "64"
                            : "No",
           cpu_has_el2_32   ? "64+32"
           : cpu_has_el2_64 ? "64"
                            : "No",
           cpu_has_el1_32   ? "64+32"
           : cpu_has_el1_64 ? "64"
                            : "No",
           cpu_has_el0_32   ? "64+32"
           : cpu_has_el0_64 ? "64"
                            : "No");
    printk("    Extensions:%s%s%s%s\n", cpu_has_fp ? " FloatingPoint" : "", cpu_has_simd ? " AdvancedSIMD" : "", cpu_has_gicv3 ? " GICv3-SysReg" : "",
           cpu_has_sve ? " SVE" : "");

    /* Warn user if we find unknown floating-point features */
    if (cpu_has_fp && (boot_cpu_feature64(fp) >= 2))
        printk(PRTOSLOG_WARNING "WARNING: Unknown Floating-point ID:%d, "
                              "this may result in corruption on the platform\n",
               boot_cpu_feature64(fp));

    /* Warn user if we find unknown AdvancedSIMD features */
    if (cpu_has_simd && (boot_cpu_feature64(simd) >= 2))
        printk(PRTOSLOG_WARNING "WARNING: Unknown AdvancedSIMD ID:%d, "
                              "this may result in corruption on the platform\n",
               boot_cpu_feature64(simd));

    printk("  Debug Features: %016" PRIx64 " %016" PRIx64 "\n", system_cpuinfo.dbg64.bits[0], system_cpuinfo.dbg64.bits[1]);
    printk("  Auxiliary Features: %016" PRIx64 " %016" PRIx64 "\n", system_cpuinfo.aux64.bits[0], system_cpuinfo.aux64.bits[1]);
    printk("  Memory Model Features: %016" PRIx64 " %016" PRIx64 "\n", system_cpuinfo.mm64.bits[0], system_cpuinfo.mm64.bits[1]);
    printk("  ISA Features:  %016" PRIx64 " %016" PRIx64 "\n", system_cpuinfo.isa64.bits[0], system_cpuinfo.isa64.bits[1]);
#endif

    /*
     * On AArch64 these refer to the capabilities when running in
     * AArch32 mode.
     */
    if (cpu_has_aarch32) {
        printk("32-bit Execution:\n");
        printk("  Processor Features: %" PRIregister ":%" PRIregister "\n", system_cpuinfo.pfr32.bits[0], system_cpuinfo.pfr32.bits[1]);
        printk("    Instruction Sets:%s%s%s%s%s%s\n", cpu_has_aarch32 ? " AArch32" : "", cpu_has_arm ? " A32" : "", cpu_has_thumb ? " Thumb" : "",
               cpu_has_thumb2 ? " Thumb-2" : "", cpu_has_thumbee ? " ThumbEE" : "", cpu_has_jazelle ? " Jazelle" : "");
        printk("    Extensions:%s%s\n", cpu_has_gentimer ? " GenericTimer" : "", cpu_has_security ? " Security" : "");

        printk("  Debug Features: %" PRIregister "\n", system_cpuinfo.dbg32.bits[0]);
        printk("  Auxiliary Features: %" PRIregister "\n", system_cpuinfo.aux32.bits[0]);
        printk("  Memory Model Features: %" PRIregister " %" PRIregister "\n"
               "                         %" PRIregister " %" PRIregister "\n",
               system_cpuinfo.mm32.bits[0], system_cpuinfo.mm32.bits[1], system_cpuinfo.mm32.bits[2], system_cpuinfo.mm32.bits[3]);
        printk("  ISA Features: %" PRIregister " %" PRIregister " %" PRIregister "\n"
               "                %" PRIregister " %" PRIregister " %" PRIregister "\n",
               system_cpuinfo.isa32.bits[0], system_cpuinfo.isa32.bits[1], system_cpuinfo.isa32.bits[2], system_cpuinfo.isa32.bits[3],
               system_cpuinfo.isa32.bits[4], system_cpuinfo.isa32.bits[5]);
    } else {
        printk("32-bit Execution: Unsupported\n");
    }

    processor_setup();
}

static void __init dt_unreserved_regions(paddr_t s, paddr_t e, void (*cb)(paddr_t ps, paddr_t pe), unsigned int first) {
    const struct membanks *reserved_mem = bootinfo_get_reserved_mem();
#ifdef CONFIG_STATIC_SHM
    const struct membanks *shmem = bootinfo_get_shmem();
    unsigned int offset;
#endif
    unsigned int i;

    /*
     * i is the current bootmodule we are evaluating across all possible
     * kinds.
     */
    for (i = first; i < reserved_mem->nr_banks; i++) {
        paddr_t r_s = reserved_mem->bank[i].start;
        paddr_t r_e = r_s + reserved_mem->bank[i].size;

        if (s < r_e && r_s < e) {
            dt_unreserved_regions(r_e, e, cb, i + 1);
            dt_unreserved_regions(s, r_s, cb, i + 1);
            return;
        }
    }

#ifdef CONFIG_STATIC_SHM
    /*
     * When retrieving the corresponding shared memory addresses
     * below, we need to index the shmem->bank starting from 0, hence
     * we need to use i - reserved_mem->nr_banks.
     */
    offset = reserved_mem->nr_banks;
    for (; i - offset < shmem->nr_banks; i++) {
        paddr_t r_s, r_e;

        r_s = shmem->bank[i - offset].start;

        /* Shared memory banks can contain INVALID_PADDR as start */
        if (INVALID_PADDR == r_s) continue;

        r_e = r_s + shmem->bank[i - offset].size;

        if (s < r_e && r_s < e) {
            dt_unreserved_regions(r_e, e, cb, i + 1);
            dt_unreserved_regions(s, r_s, cb, i + 1);
            return;
        }
    }
#endif

    cb(s, e);
}

/*
 * TODO: '*_end' could be 0 if the bank/region is at the end of the physical
 * address space. This is for now not handled as it requires more rework.
 */
static bool __init meminfo_overlap_check(const struct membanks *mem, paddr_t region_start, paddr_t region_size) {
    paddr_t bank_start = INVALID_PADDR, bank_end = 0;
    paddr_t region_end = region_start + region_size;
    unsigned int i, bank_num = mem->nr_banks;

    for (i = 0; i < bank_num; i++) {
        bank_start = mem->bank[i].start;
        bank_end = bank_start + mem->bank[i].size;

        if (INVALID_PADDR == bank_start || region_end <= bank_start || region_start >= bank_end)
            continue;
        else {
            printk("Region: [%#" PRIpaddr ", %#" PRIpaddr ") overlapping with bank[%u]: [%#" PRIpaddr ", %#" PRIpaddr ")\n", region_start, region_end, i,
                   bank_start, bank_end);
            return true;
        }
    }

    return false;
}

/*
 * TODO: '*_end' could be 0 if the module/region is at the end of the physical
 * address space. This is for now not handled as it requires more rework.
 */
static bool __init bootmodules_overlap_check(struct bootmodules *bootmodules, paddr_t region_start, paddr_t region_size) {
    paddr_t mod_start = INVALID_PADDR, mod_end = 0;
    paddr_t region_end = region_start + region_size;
    unsigned int i, mod_num = bootmodules->nr_mods;

    for (i = 0; i < mod_num; i++) {
        mod_start = bootmodules->module[i].start;
        mod_end = mod_start + bootmodules->module[i].size;

        if (region_end <= mod_start || region_start >= mod_end)
            continue;
        else {
            printk("Region: [%#" PRIpaddr ", %#" PRIpaddr ") overlapping with mod[%u]: [%#" PRIpaddr ", %#" PRIpaddr ")\n", region_start, region_end, i,
                   mod_start, mod_end);
            return true;
        }
    }

    return false;
}

void __init fw_unreserved_regions(paddr_t s, paddr_t e, void (*cb)(paddr_t ps, paddr_t pe), unsigned int first) {
    if (acpi_disabled)
        dt_unreserved_regions(s, e, cb, first);
    else
        cb(s, e);
}

/*
 * Given an input physical address range, check if this range is overlapping
 * with the existing reserved memory regions defined in bootinfo.
 * Return true if the input physical address range is overlapping with any
 * existing reserved memory regions, otherwise false.
 */
bool __init check_reserved_regions_overlap(paddr_t region_start, paddr_t region_size) {
    const struct membanks *mem_banks[] = {
        bootinfo_get_reserved_mem(),
#ifdef CONFIG_ACPI
        bootinfo_get_acpi(),
#endif
#ifdef CONFIG_STATIC_SHM
        bootinfo_get_shmem(),
#endif
    };
    unsigned int i;

    /*
     * Check if input region is overlapping with reserved memory banks or
     * ACPI EfiACPIReclaimMemory (when ACPI feature is enabled) or static
     * shared memory banks (when static shared memory feature is enabled)
     */
    for (i = 0; i < ARRAY_SIZE(mem_banks); i++)
        if (meminfo_overlap_check(mem_banks[i], region_start, region_size)) return true;

    /* Check if input region is overlapping with bootmodules */
    if (bootmodules_overlap_check(&bootinfo.modules, region_start, region_size)) return true;

    return false;
}

struct bootmodule __init *add_boot_module(bootmodule_kind kind, paddr_t start, paddr_t size, bool domU) {
    struct bootmodules *mods = &bootinfo.modules;
    struct bootmodule *mod;
    unsigned int i;

    if (mods->nr_mods == MAX_MODULES) {
        printk("Ignoring %s boot module at %" PRIpaddr "-%" PRIpaddr " (too many)\n", boot_module_kind_as_string(kind), start, start + size);
        return NULL;
    }

    if (check_reserved_regions_overlap(start, size)) return NULL;

    for (i = 0; i < mods->nr_mods; i++) {
        mod = &mods->module[i];
        if (mod->kind == kind && mod->start == start) {
            if (!domU) mod->domU = false;
            return mod;
        }
    }

    mod = &mods->module[mods->nr_mods++];
    mod->kind = kind;
    mod->start = start;
    mod->size = size;
    mod->domU = domU;

    return mod;
}

/*
 * boot_module_find_by_kind can only be used to return PRTOS modules (e.g
 * XSM, DTB) or Dom0 modules. This is not suitable for looking up guest
 * modules.
 */
struct bootmodule *__init boot_module_find_by_kind(bootmodule_kind kind) {
    struct bootmodules *mods = &bootinfo.modules;
    struct bootmodule *mod;
    int i;
    for (i = 0; i < mods->nr_mods; i++) {
        mod = &mods->module[i];
        if (mod->kind == kind && !mod->domU) return mod;
    }
    return NULL;
}

void __init add_boot_cmdline(const char *name, const char *cmdline, bootmodule_kind kind, paddr_t start, bool domU) {
    struct bootcmdlines *cmds = &bootinfo.cmdlines;
    struct bootcmdline *cmd;

    if (cmds->nr_mods == MAX_MODULES) {
        printk("Ignoring %s cmdline (too many)\n", name);
        return;
    }

    cmd = &cmds->cmdline[cmds->nr_mods++];
    cmd->kind = kind;
    cmd->domU = domU;
    cmd->start = start;

    ASSERT(strlen(name) <= DT_MAX_NAME);
    safe_strcpy(cmd->dt_name, name);

    if (strlen(cmdline) > BOOTMOD_MAX_CMDLINE) panic("module %s command line too long\n", name);
    safe_strcpy(cmd->cmdline, cmdline);
}

/*
 * boot_cmdline_find_by_kind can only be used to return PRTOS modules (e.g
 * XSM, DTB) or Dom0 modules. This is not suitable for looking up guest
 * modules.
 */
struct bootcmdline *__init boot_cmdline_find_by_kind(bootmodule_kind kind) {
    struct bootcmdlines *cmds = &bootinfo.cmdlines;
    struct bootcmdline *cmd;
    int i;

    for (i = 0; i < cmds->nr_mods; i++) {
        cmd = &cmds->cmdline[i];
        if (cmd->kind == kind && !cmd->domU) return cmd;
    }
    return NULL;
}

struct bootcmdline *__init boot_cmdline_find_by_name(const char *name) {
    struct bootcmdlines *mods = &bootinfo.cmdlines;
    struct bootcmdline *mod;
    unsigned int i;

    for (i = 0; i < mods->nr_mods; i++) {
        mod = &mods->cmdline[i];
        if (strcmp(mod->dt_name, name) == 0) return mod;
    }
    return NULL;
}

struct bootmodule *__init boot_module_find_by_addr_and_kind(bootmodule_kind kind, paddr_t start) {
    struct bootmodules *mods = &bootinfo.modules;
    struct bootmodule *mod;
    unsigned int i;

    for (i = 0; i < mods->nr_mods; i++) {
        mod = &mods->module[i];
        if (mod->kind == kind && mod->start == start) return mod;
    }
    return NULL;
}

const char *__init boot_module_kind_as_string(bootmodule_kind kind) {
    switch (kind) {
        case BOOTMOD_PRTOS:
            return "PRTOS";
        case BOOTMOD_FDT:
            return "Device Tree";
        case BOOTMOD_KERNEL:
            return "Kernel";
        case BOOTMOD_RAMDISK:
            return "Ramdisk";
        case BOOTMOD_XSM:
            return "XSM";
        case BOOTMOD_GUEST_DTB:
            return "DTB";
        case BOOTMOD_UNKNOWN:
            return "Unknown";
        default:
            BUG();
    }
}

void __init discard_initial_modules(void) {
    struct bootmodules *mi = &bootinfo.modules;
    int i;

    for (i = 0; i < mi->nr_mods; i++) {
        paddr_t s = mi->module[i].start;
        paddr_t e = s + PAGE_ALIGN(mi->module[i].size);

        if (mi->module[i].kind == BOOTMOD_PRTOS) continue;

        if (!mfn_valid(maddr_to_mfn(s)) || !mfn_valid(maddr_to_mfn(e))) continue;

        fw_unreserved_regions(s, e, init_domheap_pages, 0);
    }

    mi->nr_mods = 0;

    remove_early_mappings();
}

void __init init_pdx(void) {
    const struct membanks *mem = bootinfo_get_mem();
    paddr_t bank_start, bank_size, bank_end;

    /*
     * Arm does not have any restrictions on the bits to compress. Pass 0 to
     * let the common code further restrict the mask.
     *
     * If the logic changes in pfn_pdx_hole_setup we might have to
     * update this function too.
     */
    uint64_t mask = pdx_init_mask(0x0);
    int bank;

    for (bank = 0; bank < mem->nr_banks; bank++) {
        bank_start = mem->bank[bank].start;
        bank_size = mem->bank[bank].size;

        mask |= bank_start | pdx_region_mask(bank_start, bank_size);
    }

    for (bank = 0; bank < mem->nr_banks; bank++) {
        bank_start = mem->bank[bank].start;
        bank_size = mem->bank[bank].size;

        if (~mask & pdx_region_mask(bank_start, bank_size)) mask = 0;
    }

    pfn_pdx_hole_setup(mask >> PAGE_SHIFT);

    for (bank = 0; bank < mem->nr_banks; bank++) {
        bank_start = mem->bank[bank].start;
        bank_size = mem->bank[bank].size;
        bank_end = bank_start + bank_size;

        set_pdx_range(paddr_to_pfn(bank_start), paddr_to_pfn(bank_end));
    }
}

/*
 * Populate the boot allocator.
 * If a static heap was not provided by the admin, all the RAM but the
 * following regions will be added:
 *  - Modules (e.g., PRTOS, Kernel)
 *  - Reserved regions
 *  - PRTOSheap (arm32 only)
 * If a static heap was provided by the admin, populate the boot
 * allocator with the corresponding regions only, but with PRTOSheap excluded
 * on arm32.
 */
void __init populate_boot_allocator(void) {
    unsigned int i;
    const struct membanks *banks = bootinfo_get_mem();
    const struct membanks *reserved_mem = bootinfo_get_reserved_mem();
    paddr_t s, e;

    //     if ( bootinfo.static_heap )
    //     {
    //         for ( i = 0 ; i < reserved_mem->nr_banks; i++ )
    //         {
    //             if ( reserved_mem->bank[i].type != MEMBANK_STATIC_HEAP )
    //                 continue;

    //             s = reserved_mem->bank[i].start;
    //             e = s + reserved_mem->bank[i].size;
    // #ifdef CONFIG_ARM_32
    //             /* Avoid the prtosheap, note that the prtosheap cannot across a bank */
    //             if ( s <= mfn_to_maddr(directmap_mfn_start) &&
    //                  e >= mfn_to_maddr(directmap_mfn_end) )
    //             {
    //                 init_boot_pages(s, mfn_to_maddr(directmap_mfn_start));
    //                 init_boot_pages(mfn_to_maddr(directmap_mfn_end), e);
    //             }
    //             else
    // #endif
    //                 init_boot_pages(s, e);
    //         }

    //         return;
    //     }

    //     for ( i = 0; i < banks->nr_banks; i++ )
    //     {
    //         const struct membank *bank = &banks->bank[i];
    //         paddr_t bank_end = bank->start + bank->size;

    //         s = bank->start;
    //         while ( s < bank_end )
    //         {
    //             paddr_t n = bank_end;

    //             e = next_module(s, &n);

    //             if ( e == ~(paddr_t)0 )
    //                 e = n = bank_end;

    //             /*
    //              * Module in a RAM bank other than the one which we are
    //              * not dealing with here.
    //              */
    //             if ( e > bank_end )
    //                 e = bank_end;

    // #ifdef CONFIG_ARM_32
    //             /* Avoid the prtosheap */
    //             if ( s < mfn_to_maddr(directmap_mfn_end) &&
    //                  mfn_to_maddr(directmap_mfn_start) < e )
    //             {
    //                 e = mfn_to_maddr(directmap_mfn_start);
    //                 n = mfn_to_maddr(directmap_mfn_end);
    //             }
    // #endif

    //             fw_unreserved_regions(s, e, init_boot_pages, 0);
    //             s = n;
    //         }
    //     }

    fw_unreserved_regions(0x40000000, 0x49000000, init_boot_pages, 0);   // WA for PRTOS
    fw_unreserved_regions(0x4a000000, 0x140000000, init_boot_pages, 0);  // WA for PRTOS
}

size_t __read_mostly dcache_line_bytes;

// hexdump -e '16/1 "0x%02X, " "\n"' virt-gicv3.dtb  > log.txt
// copy the value from log.txt to the g_data array below
// The g_data array is used to store the GICv3 device tree binary data
// The data is used to initialize the GICv3 device tree in the kernel
// The data is in the format of a device tree binary, which is a data structure used
// to describe the hardware components of a system in a platform-independent way.
const unsigned char g_data[] = {
    0xD0, 0x0D, 0xFE, 0xED, 0x00, 0x00, 0x20, 0xB2, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x1E, 0xAC, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x00, 0x00, 0x1E, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x11, 0x6C, 0x69, 0x6E, 0x75, 0x78, 0x2C, 0x64, 0x75, 0x6D, 0x6D, 0x79, 0x2D,
    0x76, 0x69, 0x72, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x32,
    0x6C, 0x69, 0x6E, 0x75, 0x78, 0x2C, 0x64, 0x75, 0x6D, 0x6D, 0x79, 0x2D, 0x76, 0x69, 0x72, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x70, 0x73,
    0x63, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x3D, 0xC4, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x45, 0xC4, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x4C, 0x84, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x54, 0xC4, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x60, 0x73, 0x6D, 0x63, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C, 0x70, 0x73,
    0x63, 0x69, 0x2D, 0x31, 0x2E, 0x30, 0x00, 0x61, 0x72, 0x6D, 0x2C, 0x70, 0x73, 0x63, 0x69, 0x2D, 0x30, 0x2E, 0x32, 0x00, 0x61, 0x72, 0x6D, 0x2C, 0x70, 0x73,
    0x63, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x6D, 0x65, 0x6D, 0x6F, 0x72, 0x79, 0x40, 0x34, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x6B, 0x6D, 0x65, 0x6D, 0x6F, 0x72, 0x79, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x70, 0x6C, 0x61, 0x74, 0x66, 0x6F, 0x72, 0x6D, 0x2D, 0x62, 0x75, 0x73, 0x40, 0x63, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x32, 0x71, 0x65, 0x6D, 0x75, 0x2C, 0x70, 0x6C, 0x61, 0x74, 0x66, 0x6F, 0x72,
    0x6D, 0x00, 0x73, 0x69, 0x6D, 0x70, 0x6C, 0x65, 0x2D, 0x62, 0x75, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x66, 0x77,
    0x2D, 0x63, 0x66, 0x67, 0x40, 0x39, 0x30, 0x32, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x09, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x32, 0x71, 0x65, 0x6D, 0x75, 0x2C, 0x66, 0x77, 0x2D, 0x63, 0x66, 0x67, 0x2D,
    0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69,
    0x6F, 0x40, 0x61, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x30, 0x32, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F,
    0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40,
    0x61, 0x30, 0x30, 0x30, 0x34, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x30, 0x36, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D,
    0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30,
    0x30, 0x30, 0x38, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69,
    0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x30, 0x61, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69,
    0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x30,
    0x63, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00,
    0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67,
    0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00,
    0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74,
    0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x30, 0x65, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x31, 0x30, 0x30,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00,
    0x00, 0x00, 0x0A, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32,
    0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F,
    0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x31, 0x32, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x31, 0x34, 0x30, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69,
    0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D,
    0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x31, 0x36, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x31, 0x38, 0x30, 0x30, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74,
    0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69,
    0x6F, 0x40, 0x61, 0x30, 0x30, 0x31, 0x61, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x31, 0x63, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1E, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x1C, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F,
    0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40,
    0x61, 0x30, 0x30, 0x31, 0x65, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x32, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D,
    0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30,
    0x30, 0x32, 0x32, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69,
    0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x32, 0x34, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69,
    0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x32,
    0x36, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00,
    0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67,
    0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00,
    0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74,
    0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x32, 0x38, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x32, 0x61, 0x30,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00,
    0x00, 0x00, 0x0A, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32,
    0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F,
    0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x32, 0x63, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x32, 0x65, 0x30, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69,
    0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D,
    0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x33, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x33, 0x32, 0x30, 0x30, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
    0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74,
    0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69,
    0x6F, 0x40, 0x61, 0x30, 0x30, 0x33, 0x34, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x33, 0x36, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x36, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F,
    0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40,
    0x61, 0x30, 0x30, 0x33, 0x38, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x33, 0x61, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x3A, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D,
    0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30,
    0x30, 0x33, 0x63, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C,
    0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x76, 0x69,
    0x72, 0x74, 0x69, 0x6F, 0x5F, 0x6D, 0x6D, 0x69, 0x6F, 0x40, 0x61, 0x30, 0x30, 0x33, 0x65, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x76, 0x69, 0x72, 0x74, 0x69, 0x6F, 0x2C, 0x6D, 0x6D, 0x69,
    0x6F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x67, 0x70, 0x69, 0x6F, 0x2D, 0x6B, 0x65, 0x79, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x32, 0x67, 0x70, 0x69, 0x6F, 0x2D, 0x6B, 0x65, 0x79, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x70, 0x6F,
    0x77, 0x65, 0x72, 0x6F, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x80, 0x07,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x9C, 0x00, 0x00, 0x00, 0x74, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0xA7, 0x47, 0x50, 0x49, 0x4F, 0x20, 0x4B, 0x65, 0x79, 0x20, 0x50, 0x6F, 0x77, 0x65, 0x72, 0x6F, 0x66,
    0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x70, 0x6C, 0x30, 0x36, 0x31, 0x40, 0x39, 0x30, 0x33, 0x30,
    0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x80, 0x07, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB5, 0x61, 0x70, 0x62, 0x5F, 0x70, 0x63, 0x6C, 0x6B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0xC1, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0xD8, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xE4, 0x64, 0x69, 0x73, 0x61,
    0x62, 0x6C, 0x65, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x09, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x70, 0x63, 0x69, 0x65, 0x40, 0x31, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xEB, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x02, 0x80, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x80, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x18, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x0C, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3E, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2E, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x01, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x01, 0x25, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6B, 0x70, 0x63, 0x69, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00,
    0x00, 0x32, 0x70, 0x63, 0x69, 0x2D, 0x68, 0x6F, 0x73, 0x74, 0x2D, 0x65, 0x63, 0x61, 0x6D, 0x2D, 0x67, 0x65, 0x6E, 0x65, 0x72, 0x69, 0x63, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x70, 0x6C, 0x30, 0x33, 0x31, 0x40, 0x39, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xB5, 0x61, 0x70, 0x62, 0x5F, 0x70, 0x63, 0x6C, 0x6B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xC1, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72,
    0x6D, 0x2C, 0x70, 0x6C, 0x30, 0x33, 0x31, 0x00, 0x61, 0x72, 0x6D, 0x2C, 0x70, 0x72, 0x69, 0x6D, 0x65, 0x63, 0x65, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01, 0x70, 0x6C, 0x30, 0x31, 0x31, 0x40, 0x39, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x11, 0x00, 0x00, 0x00, 0xB5, 0x75, 0x61, 0x72, 0x74, 0x63, 0x6C, 0x6B, 0x00, 0x61, 0x70, 0x62, 0x5F, 0x70, 0x63, 0x6C, 0x6B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xC1, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C, 0x70, 0x6C, 0x30, 0x31, 0x31, 0x00, 0x61, 0x72, 0x6D, 0x2C, 0x70, 0x72, 0x69, 0x6D, 0x65, 0x63,
    0x65, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x70, 0x6D, 0x75, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00,
    0x00, 0x8B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x32,
    0x61, 0x72, 0x6D, 0x2C, 0x61, 0x72, 0x6D, 0x76, 0x38, 0x2D, 0x70, 0x6D, 0x75, 0x76, 0x33, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x69, 0x6E,
    0x74, 0x63, 0x40, 0x38, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xAD,
    0x00, 0x00, 0x80, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C,
    0x67, 0x69, 0x63, 0x2D, 0x76, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x57, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x0C, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x69, 0x74, 0x73, 0x40, 0x38, 0x30, 0x38, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x6C, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x77, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72,
    0x6D, 0x2C, 0x67, 0x69, 0x63, 0x2D, 0x76, 0x33, 0x2D, 0x69, 0x74, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x66, 0x6C, 0x61, 0x73, 0x68, 0x40, 0x30, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x86, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00,
    0x00, 0x32, 0x63, 0x66, 0x69, 0x2D, 0x66, 0x6C, 0x61, 0x73, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x70, 0x75, 0x73,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x63, 0x70, 0x75, 0x2D, 0x6D, 0x61, 0x70, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x73, 0x6F, 0x63, 0x6B, 0x65, 0x74, 0x30, 0x00, 0x00, 0x00, 0x00, 0x01, 0x63, 0x6C, 0x75, 0x73, 0x74, 0x65, 0x72, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x63, 0x6F, 0x72, 0x65, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x91, 0x00, 0x00, 0x80, 0x04,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x6F, 0x72, 0x65, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
    0x01, 0x91, 0x00, 0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x6F, 0x72, 0x65, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x91, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x6F, 0x72, 0x65, 0x33, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x91, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x70, 0x75, 0x40, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x80, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x95, 0x70, 0x73, 0x63, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x0F, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C, 0x63, 0x6F, 0x72, 0x74, 0x65, 0x78, 0x2D, 0x61, 0x35, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6B, 0x63, 0x70, 0x75, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x70, 0x75, 0x40, 0x31, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x95, 0x70, 0x73, 0x63, 0x69, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C, 0x63, 0x6F, 0x72, 0x74, 0x65, 0x78, 0x2D, 0x61,
    0x35, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6B, 0x63, 0x70, 0x75, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x01, 0x63, 0x70, 0x75, 0x40, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x80, 0x02,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00,
    0x01, 0x95, 0x70, 0x73, 0x63, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C,
    0x63, 0x6F, 0x72, 0x74, 0x65, 0x78, 0x2D, 0x61, 0x35, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6B, 0x63, 0x70,
    0x75, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x70, 0x75, 0x40, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x95, 0x70, 0x73, 0x63, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0F,
    0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C, 0x63, 0x6F, 0x72, 0x74, 0x65, 0x78, 0x2D, 0x61, 0x35, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x6B, 0x63, 0x70, 0x75, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x74, 0x69, 0x6D, 0x65,
    0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xA3, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x32, 0x61, 0x72, 0x6D, 0x2C, 0x61, 0x72, 0x6D, 0x76, 0x38, 0x2D, 0x74, 0x69, 0x6D, 0x65, 0x72, 0x00,
    0x61, 0x72, 0x6D, 0x2C, 0x61, 0x72, 0x6D, 0x76, 0x37, 0x2D, 0x74, 0x69, 0x6D, 0x65, 0x72, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x61, 0x70,
    0x62, 0x2D, 0x70, 0x63, 0x6C, 0x6B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x01, 0xAD, 0x63, 0x6C, 0x6B, 0x32, 0x34, 0x6D, 0x68, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0xC0, 0x01, 0x6E, 0x36, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0xD0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x32, 0x66, 0x69, 0x78, 0x65, 0x64, 0x2D, 0x63, 0x6C, 0x6F, 0x63,
    0x6B, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x63, 0x68, 0x6F, 0x73, 0x65, 0x6E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0F,
    0x00, 0x00, 0x01, 0xDD, 0x2F, 0x70, 0x6C, 0x30, 0x31, 0x31, 0x40, 0x39, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x01, 0xE9, 0x46, 0x28, 0x37, 0x31, 0x7E, 0xCB, 0x79, 0xE2, 0xA8, 0xB5, 0x54, 0x5C, 0xD7, 0x1C, 0x58, 0x86, 0x52, 0x5A, 0x2C, 0x65,
    0x6C, 0x72, 0x06, 0x10, 0x2A, 0x0C, 0x4D, 0xE5, 0xAD, 0xAC, 0x4B, 0x8E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x01, 0xF2, 0xF9, 0x3D,
    0xA4, 0xCA, 0x9B, 0x64, 0x5B, 0x59, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x6D, 0x6F, 0x64, 0x75, 0x6C, 0x65, 0x40, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x32, 0x78, 0x65, 0x6E, 0x2C, 0x6C, 0x69, 0x6E, 0x75, 0x78, 0x2D, 0x7A, 0x69,
    0x6D, 0x61, 0x67, 0x65, 0x00, 0x78, 0x65, 0x6E, 0x2C, 0x6D, 0x75, 0x6C, 0x74, 0x69, 0x62, 0x6F, 0x6F, 0x74, 0x2D, 0x6D, 0x6F, 0x64, 0x75, 0x6C, 0x65, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x67, 0x47, 0x00, 0x00, 0x00, 0x00, 0xDB, 0x6A, 0x19, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x5C, 0x00, 0x00, 0x01, 0xFD, 0x72, 0x77, 0x20, 0x72, 0x6F, 0x6F, 0x74, 0x3D, 0x2F, 0x64, 0x65, 0x76, 0x2F, 0x72, 0x61, 0x6D, 0x20, 0x72,
    0x64, 0x69, 0x6E, 0x69, 0x74, 0x3D, 0x2F, 0x73, 0x62, 0x69, 0x6E, 0x2F, 0x69, 0x6E, 0x69, 0x74, 0x20, 0x65, 0x61, 0x72, 0x6C, 0x79, 0x70, 0x72, 0x69, 0x6E,
    0x74, 0x6B, 0x3D, 0x73, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x2C, 0x74, 0x74, 0x79, 0x41, 0x4D, 0x41, 0x30, 0x20, 0x63, 0x6F, 0x6E, 0x73, 0x6F, 0x6C, 0x65, 0x3D,
    0x68, 0x76, 0x63, 0x30, 0x20, 0x65, 0x61, 0x72, 0x6C, 0x79, 0x63, 0x6F, 0x6E, 0x3D, 0x78, 0x65, 0x6E, 0x62, 0x6F, 0x6F, 0x74, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01, 0x6D, 0x6F, 0x64, 0x75, 0x6C, 0x65, 0x40, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00,
    0x00, 0x32, 0x78, 0x65, 0x6E, 0x2C, 0x6C, 0x69, 0x6E, 0x75, 0x78, 0x2D, 0x69, 0x6E, 0x69, 0x74, 0x72, 0x64, 0x00, 0x78, 0x65, 0x6E, 0x2C, 0x6D, 0x75, 0x6C,
    0x74, 0x69, 0x62, 0x6F, 0x6F, 0x74, 0x2D, 0x6D, 0x6F, 0x64, 0x75, 0x6C, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
    0x00, 0x67, 0x42, 0x00, 0x00, 0x00, 0x00, 0x12, 0x2A, 0x4A, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x09,
    0x69, 0x6E, 0x74, 0x65, 0x72, 0x72, 0x75, 0x70, 0x74, 0x2D, 0x70, 0x61, 0x72, 0x65, 0x6E, 0x74, 0x00, 0x6D, 0x6F, 0x64, 0x65, 0x6C, 0x00, 0x23, 0x73, 0x69,
    0x7A, 0x65, 0x2D, 0x63, 0x65, 0x6C, 0x6C, 0x73, 0x00, 0x23, 0x61, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73, 0x2D, 0x63, 0x65, 0x6C, 0x6C, 0x73, 0x00, 0x63, 0x6F,
    0x6D, 0x70, 0x61, 0x74, 0x69, 0x62, 0x6C, 0x65, 0x00, 0x6D, 0x69, 0x67, 0x72, 0x61, 0x74, 0x65, 0x00, 0x63, 0x70, 0x75, 0x5F, 0x6F, 0x6E, 0x00, 0x63, 0x70,
    0x75, 0x5F, 0x6F, 0x66, 0x66, 0x00, 0x63, 0x70, 0x75, 0x5F, 0x73, 0x75, 0x73, 0x70, 0x65, 0x6E, 0x64, 0x00, 0x6D, 0x65, 0x74, 0x68, 0x6F, 0x64, 0x00, 0x72,
    0x65, 0x67, 0x00, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x5F, 0x74, 0x79, 0x70, 0x65, 0x00, 0x72, 0x61, 0x6E, 0x67, 0x65, 0x73, 0x00, 0x64, 0x6D, 0x61, 0x2D,
    0x63, 0x6F, 0x68, 0x65, 0x72, 0x65, 0x6E, 0x74, 0x00, 0x69, 0x6E, 0x74, 0x65, 0x72, 0x72, 0x75, 0x70, 0x74, 0x73, 0x00, 0x67, 0x70, 0x69, 0x6F, 0x73, 0x00,
    0x6C, 0x69, 0x6E, 0x75, 0x78, 0x2C, 0x63, 0x6F, 0x64, 0x65, 0x00, 0x6C, 0x61, 0x62, 0x65, 0x6C, 0x00, 0x70, 0x68, 0x61, 0x6E, 0x64, 0x6C, 0x65, 0x00, 0x63,
    0x6C, 0x6F, 0x63, 0x6B, 0x2D, 0x6E, 0x61, 0x6D, 0x65, 0x73, 0x00, 0x63, 0x6C, 0x6F, 0x63, 0x6B, 0x73, 0x00, 0x67, 0x70, 0x69, 0x6F, 0x2D, 0x63, 0x6F, 0x6E,
    0x74, 0x72, 0x6F, 0x6C, 0x6C, 0x65, 0x72, 0x00, 0x23, 0x67, 0x70, 0x69, 0x6F, 0x2D, 0x63, 0x65, 0x6C, 0x6C, 0x73, 0x00, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73,
    0x00, 0x69, 0x6E, 0x74, 0x65, 0x72, 0x72, 0x75, 0x70, 0x74, 0x2D, 0x6D, 0x61, 0x70, 0x2D, 0x6D, 0x61, 0x73, 0x6B, 0x00, 0x69, 0x6E, 0x74, 0x65, 0x72, 0x72,
    0x75, 0x70, 0x74, 0x2D, 0x6D, 0x61, 0x70, 0x00, 0x23, 0x69, 0x6E, 0x74, 0x65, 0x72, 0x72, 0x75, 0x70, 0x74, 0x2D, 0x63, 0x65, 0x6C, 0x6C, 0x73, 0x00, 0x6D,
    0x73, 0x69, 0x2D, 0x6D, 0x61, 0x70, 0x00, 0x62, 0x75, 0x73, 0x2D, 0x72, 0x61, 0x6E, 0x67, 0x65, 0x00, 0x6C, 0x69, 0x6E, 0x75, 0x78, 0x2C, 0x70, 0x63, 0x69,
    0x2D, 0x64, 0x6F, 0x6D, 0x61, 0x69, 0x6E, 0x00, 0x23, 0x72, 0x65, 0x64, 0x69, 0x73, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x6F, 0x72, 0x2D, 0x72, 0x65, 0x67,
    0x69, 0x6F, 0x6E, 0x73, 0x00, 0x69, 0x6E, 0x74, 0x65, 0x72, 0x72, 0x75, 0x70, 0x74, 0x2D, 0x63, 0x6F, 0x6E, 0x74, 0x72, 0x6F, 0x6C, 0x6C, 0x65, 0x72, 0x00,
    0x23, 0x6D, 0x73, 0x69, 0x2D, 0x63, 0x65, 0x6C, 0x6C, 0x73, 0x00, 0x6D, 0x73, 0x69, 0x2D, 0x63, 0x6F, 0x6E, 0x74, 0x72, 0x6F, 0x6C, 0x6C, 0x65, 0x72, 0x00,
    0x62, 0x61, 0x6E, 0x6B, 0x2D, 0x77, 0x69, 0x64, 0x74, 0x68, 0x00, 0x63, 0x70, 0x75, 0x00, 0x65, 0x6E, 0x61, 0x62, 0x6C, 0x65, 0x2D, 0x6D, 0x65, 0x74, 0x68,
    0x6F, 0x64, 0x00, 0x61, 0x6C, 0x77, 0x61, 0x79, 0x73, 0x2D, 0x6F, 0x6E, 0x00, 0x63, 0x6C, 0x6F, 0x63, 0x6B, 0x2D, 0x6F, 0x75, 0x74, 0x70, 0x75, 0x74, 0x2D,
    0x6E, 0x61, 0x6D, 0x65, 0x73, 0x00, 0x63, 0x6C, 0x6F, 0x63, 0x6B, 0x2D, 0x66, 0x72, 0x65, 0x71, 0x75, 0x65, 0x6E, 0x63, 0x79, 0x00, 0x23, 0x63, 0x6C, 0x6F,
    0x63, 0x6B, 0x2D, 0x63, 0x65, 0x6C, 0x6C, 0x73, 0x00, 0x73, 0x74, 0x64, 0x6F, 0x75, 0x74, 0x2D, 0x70, 0x61, 0x74, 0x68, 0x00, 0x72, 0x6E, 0x67, 0x2D, 0x73,
    0x65, 0x65, 0x64, 0x00, 0x6B, 0x61, 0x73, 0x6C, 0x72, 0x2D, 0x73, 0x65, 0x65, 0x64, 0x00, 0x62, 0x6F, 0x6F, 0x74, 0x61, 0x72, 0x67, 0x73, 0x00};

void init_percpu_areas_prtos(void) {
    dcache_line_bytes = read_dcache_line_bytes();
    percpu_init_areas();
    set_processor_id(0); /* needed early, for smp_processor_id() */
}


void kick_cpus_prtos(void) {
    int i;
    for_each_present_cpu(i) {
        printk("#########################Bringing up CPU %u\n", i);
        if ((num_online_cpus() < nr_cpu_ids) && !cpu_online(i)) {
            int ret = cpu_up_prtos(i);
            if (ret != 0) printk("Failed to bring up CPU %u (error %d)\n", i, ret);
        }
    }
}

void arch_get_prtos_caps(prtos_capabilities_info_t *info) {
    /* Interface name is always prtos-3.0-* for PRTOS-3.x. */
    int major = 3, minor = 0;
    char s[32];

    (*info)[0] = '\0';

#ifdef CONFIG_ARM_64
    snprintf(s, sizeof(s), "prtos-%d.%d-aarch64 ", major, minor);
    safe_strcat(*info, s);
#endif
    if (cpu_has_aarch32) {
        snprintf(s, sizeof(s), "prtos-%d.%d-armv7l ", major, minor);
        safe_strcat(*info, s);
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_setup.c === */
/* === BEGIN INLINED: arch_arm_smp.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <prtos_mm.h>
#include <asm_system.h>
#include <asm_smp.h>
#include <asm_page.h>
#include <asm_gic.h>
#include <asm_flushtlb.h>

void arch_flush_tlb_mask(const cpumask_t *mask)
{
    /* No need to IPI other processors on ARM, the processor takes care of it. */
    flush_all_guests_tlb();
}

void smp_send_event_check_mask(const cpumask_t *mask)
{
    send_SGI_mask(mask, GIC_SGI_EVENT_CHECK);
}

void smp_send_call_function_mask(const cpumask_t *mask)
{
    cpumask_t target_mask;

    cpumask_andnot(&target_mask, mask, cpumask_of(smp_processor_id()));

    send_SGI_mask(&target_mask, GIC_SGI_CALL_FUNCTION);

    if ( cpumask_test_cpu(smp_processor_id(), mask) )
    {
        local_irq_disable();
        smp_call_function_interrupt();
        local_irq_enable();
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_smp.c === */
/* === BEGIN INLINED: arch_arm_traps.c === */
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/traps.c
 *
 * ARM Trap handlers
 *
 * Copyright (c) 2011 Citrix Systems.
 */

#include <prtos_prtos_config.h>

#include <prtos_acpi.h>
#include <prtos_domain_page.h>
#include <prtos_errno.h>
#include <prtos_hypercall.h>
#include <prtos_init.h>
#include <prtos_iocap.h>
#include <prtos_ioreq.h>
#include <prtos_irq.h>
#include <prtos_lib.h>
#include <prtos_mem_access.h>
#include <prtos_mm.h>
#include <prtos_param.h>
#include <prtos_perfc.h>
#include <prtos_smp.h>
#include <prtos_softirq.h>
#include <prtos_string.h>
#include <prtos_symbols.h>
#include <prtos_version.h>
#include <prtos_virtual_region.h>
#include <prtos_vpci.h>

#include <public_sched.h>
#include <public_prtos.h>

#include <asm_cpuerrata.h>
#include <asm_cpufeature.h>
#include <asm_event.h>
#include <asm_hsr.h>
#include <asm_mem_access.h>
#include <asm_mmio.h>
#include <asm_regs.h>
#include <asm_setup.h>
#include <asm_smccc.h>
#include <asm_traps.h>
#include <asm_vgic.h>
#include <asm_vtimer.h>

/*
 * partial_emulation: If true, partial emulation for system/coprocessor
 * registers will be enabled.
 */
#ifdef CONFIG_PARTIAL_EMULATION
bool __ro_after_init partial_emulation = false;
boolean_param("partial-emulation", partial_emulation);
#endif

/* The base of the stack must always be double-word aligned, which means
 * that both the kernel half of struct cpu_user_regs (which is pushed in
 * entry.S) and struct cpu_info (which lives at the bottom of a PRTOS
 * stack) must be doubleword-aligned in size.  */
static void __init __maybe_unused build_assertions(void) {
#ifdef CONFIG_ARM_64
    BUILD_BUG_ON((sizeof(struct cpu_user_regs)) & 0xf);
    BUILD_BUG_ON((offsetof(struct cpu_user_regs, spsr_el1)) & 0xf);
    BUILD_BUG_ON((offsetof(struct cpu_user_regs, lr)) & 0xf);
    BUILD_BUG_ON((sizeof(struct cpu_info)) & 0xf);
#else
    BUILD_BUG_ON((sizeof(struct cpu_user_regs)) & 0x7);
    BUILD_BUG_ON((offsetof(struct cpu_user_regs, sp_usr)) & 0x7);
    BUILD_BUG_ON((sizeof(struct cpu_info)) & 0x7);
#endif
}

#ifdef CONFIG_ARM_32
static int debug_stack_lines = 20;
#define stack_words_per_line 8
#else
static int debug_stack_lines = 40;
#define stack_words_per_line 4
#endif

integer_param("debug_stack_lines", debug_stack_lines);

static enum {
    TRAP,
    NATIVE,
} vwfi;

static int __init parse_vwfi(const char *s) {
    if (!strcmp(s, "native"))
        vwfi = NATIVE;
    else
        vwfi = TRAP;

    return 0;
}
custom_param("vwfi", parse_vwfi);

register_t get_default_hcr_flags(void) {
    return (HCR_PTW | HCR_BSU_INNER | HCR_AMO | HCR_IMO | HCR_FMO | HCR_VM | (vwfi != NATIVE ? (HCR_TWI | HCR_TWE) : 0) | HCR_TID3 | HCR_TSC | HCR_TAC |
            HCR_SWIO | HCR_TIDCP | HCR_FB | HCR_TSW);
}

register_t get_default_cptr_flags(void) {
    /*
     * Trap all coprocessor registers (0-13) except cp10 and
     * cp11 for VFP.
     *
     * /!\ All coprocessors except cp10 and cp11 cannot be used in PRTOS.
     *
     * On ARM64 the TCPx bits which we set here (0..9,12,13) are all
     * RES1, i.e. they would trap whether we did this write or not.
     */
    return ((HCPTR_CP_MASK & ~(HCPTR_CP(10) | HCPTR_CP(11))) | HCPTR_TTA | HCPTR_TAM);
}

static enum {
    SERRORS_DIVERSE,
    SERRORS_PANIC,
} serrors_op = SERRORS_DIVERSE;

static int __init parse_serrors_behavior(const char *str) {
    if (!strcmp(str, "panic"))
        serrors_op = SERRORS_PANIC;
    else if (!strcmp(str, "diverse"))
        serrors_op = SERRORS_DIVERSE;
    else
        return -EINVAL;

    return 0;
}
custom_param("serrors", parse_serrors_behavior);

static int __init update_serrors_cpu_caps(void) {
    if (serrors_op != SERRORS_DIVERSE) cpus_set_cap(SKIP_SYNCHRONIZE_SERROR_ENTRY_EXIT);

    return 0;
}
__initcall(update_serrors_cpu_caps);

void init_traps(void) {
    /*
     * Setup Hyp vector base. Note they might get updated with the
     * branch predictor hardening.
     */
    WRITE_SYSREG((vaddr_t)hyp_traps_vector, VBAR_EL2);

    /* Trap Debug and Performance Monitor accesses */
    WRITE_SYSREG(HDCR_TDRA | HDCR_TDOSA | HDCR_TDA | HDCR_TPM | HDCR_TPMCR, MDCR_EL2);

    /* Trap CP15 c15 used for implementation defined registers */
    WRITE_SYSREG(HSTR_T(15), HSTR_EL2);

    WRITE_SYSREG(get_default_cptr_flags(), CPTR_EL2);

    /*
     * Configure HCR_EL2 with the bare minimum to run PRTOS until a guest
     * is scheduled. {A,I,F}MO bits are set to allow EL2 receiving
     * interrupts.
     */
    WRITE_SYSREG(HCR_AMO | HCR_FMO | HCR_IMO, HCR_EL2);
    isb();
}


/* XXX could/should be common code */
static void print_prtos_info(void) {
    char taint_str[TAINT_STRING_MAX_LEN];

    printk("----[ PRTOS-%d.%d%s  %s  %s  %s ]----\n", prtos_major_version(), prtos_minor_version(), prtos_extra_version(),
#ifdef CONFIG_ARM_32
           "arm32",
#else
           "arm64",
#endif
           prtos_build_info(), print_tainted(taint_str));
}

#ifdef CONFIG_ARM_32
static inline bool is_zero_register(int reg) {
    /* There is no zero register for ARM32 */
    return false;
}
#else
static inline bool is_zero_register(int reg) {
    /*
     * For store/load and sysreg instruction, the encoding 31 always
     * corresponds to {w,x}zr which is the zero register.
     */
    return (reg == 31);
}
#endif

/*
 * Returns a pointer to the given register value in regs, taking the
 * processor mode (CPSR) into account.
 *
 * Note that this function should not be used directly but via
 * {get,set}_user_reg.
 */
static register_t *select_user_reg(struct cpu_user_regs *regs, int reg) {
    BUG_ON(!guest_mode(regs));

#ifdef CONFIG_ARM_32
    /*
     * We rely heavily on the layout of cpu_user_regs to avoid having
     * to handle all of the registers individually. Use BUILD_BUG_ON to
     * ensure that things which expect are contiguous actually are.
     */
#define REGOFFS(R) offsetof(struct cpu_user_regs, R)

    switch (reg) {
        case 0 ... 7: /* Unbanked registers */
            BUILD_BUG_ON(REGOFFS(r0) + 7 * sizeof(register_t) != REGOFFS(r7));
            return &regs->r0 + reg;
        case 8 ... 12: /* Register banked in FIQ mode */
            BUILD_BUG_ON(REGOFFS(r8_fiq) + 4 * sizeof(register_t) != REGOFFS(r12_fiq));
            if (fiq_mode(regs))
                return &regs->r8_fiq + reg - 8;
            else
                return &regs->r8 + reg - 8;
        case 13 ... 14: /* Banked SP + LR registers */
            BUILD_BUG_ON(REGOFFS(sp_fiq) + 1 * sizeof(register_t) != REGOFFS(lr_fiq));
            BUILD_BUG_ON(REGOFFS(sp_irq) + 1 * sizeof(register_t) != REGOFFS(lr_irq));
            BUILD_BUG_ON(REGOFFS(sp_svc) + 1 * sizeof(register_t) != REGOFFS(lr_svc));
            BUILD_BUG_ON(REGOFFS(sp_abt) + 1 * sizeof(register_t) != REGOFFS(lr_abt));
            BUILD_BUG_ON(REGOFFS(sp_und) + 1 * sizeof(register_t) != REGOFFS(lr_und));
            switch (regs->cpsr & PSR_MODE_MASK) {
                case PSR_MODE_USR:
                case PSR_MODE_SYS: /* Sys regs are the usr regs */
                    if (reg == 13)
                        return &regs->sp_usr;
                    else /* lr_usr == lr in a user frame */
                        return &regs->lr;
                case PSR_MODE_FIQ:
                    return &regs->sp_fiq + reg - 13;
                case PSR_MODE_IRQ:
                    return &regs->sp_irq + reg - 13;
                case PSR_MODE_SVC:
                    return &regs->sp_svc + reg - 13;
                case PSR_MODE_ABT:
                    return &regs->sp_abt + reg - 13;
                case PSR_MODE_UND:
                    return &regs->sp_und + reg - 13;
                case PSR_MODE_MON:
                case PSR_MODE_HYP:
                default:
                    BUG();
            }
        case 15: /* PC */
            return &regs->pc;
        default:
            BUG();
    }
#undef REGOFFS
#else
    /*
     * On 64-bit the syndrome register contains the register index as
     * viewed in AArch64 state even if the trap was from AArch32 mode.
     */
    BUG_ON(is_zero_register(reg)); /* Cannot be {w,x}zr */
    return &regs->x0 + reg;
#endif
}

register_t get_user_reg(struct cpu_user_regs *regs, int reg) {
    if (is_zero_register(reg)) return 0;

    return *select_user_reg(regs, reg);
}

void set_user_reg(struct cpu_user_regs *regs, int reg, register_t value) {
    if (is_zero_register(reg)) return;

    *select_user_reg(regs, reg) = value;
}

static const char *decode_fsc(uint32_t fsc, int *level) {
    const char *msg = NULL;

    switch (fsc & 0x3f) {
        case FSC_FLT_TRANS ... FSC_FLT_TRANS + 3:
            msg = "Translation fault";
            *level = fsc & FSC_LL_MASK;
            break;
        case FSC_FLT_ACCESS ... FSC_FLT_ACCESS + 3:
            msg = "Access fault";
            *level = fsc & FSC_LL_MASK;
            break;
        case FSC_FLT_PERM ... FSC_FLT_PERM + 3:
            msg = "Permission fault";
            *level = fsc & FSC_LL_MASK;
            break;

        case FSC_SEA:
            msg = "Synchronous External Abort";
            break;
        case FSC_SPE:
            msg = "Memory Access Synchronous Parity Error";
            break;
        case FSC_APE:
            msg = "Memory Access Asynchronous Parity Error";
            break;
        case FSC_SEATT ... FSC_SEATT + 3:
            msg = "Sync. Ext. Abort Translation Table";
            *level = fsc & FSC_LL_MASK;
            break;
        case FSC_SPETT ... FSC_SPETT + 3:
            msg = "Sync. Parity. Error Translation Table";
            *level = fsc & FSC_LL_MASK;
            break;
        case FSC_AF:
            msg = "Alignment Fault";
            break;
        case FSC_DE:
            msg = "Debug Event";
            break;

        case FSC_LKD:
            msg = "Implementation Fault: Lockdown Abort";
            break;
        case FSC_CPR:
            msg = "Implementation Fault: Coprocossor Abort";
            break;

        default:
            msg = "Unknown Failure";
            break;
    }
    return msg;
}

static const char *fsc_level_str(int level) {
    switch (level) {
        case -1:
            return "";
        case 1:
            return " at level 1";
        case 2:
            return " at level 2";
        case 3:
            return " at level 3";
        default:
            return " (level invalid)";
    }
}

void panic_PAR(uint64_t par) {
    const char *msg;
    int level = -1;
    int stage = par & PAR_STAGE2 ? 2 : 1;
    int second_in_first = !!(par & PAR_STAGE21);

    msg = decode_fsc((par & PAR_FSC_MASK) >> PAR_FSC_SHIFT, &level);

    printk("PAR: %016" PRIx64 ": %s stage %d%s%s\n", par, msg, stage, second_in_first ? " during second stage lookup" : "", fsc_level_str(level));

    panic("Error during Hypervisor-to-physical address translation\n");
}

static void cpsr_switch_mode(struct cpu_user_regs *regs, int mode) {
    register_t sctlr = READ_SYSREG(SCTLR_EL1);

    regs->cpsr &= ~(PSR_MODE_MASK | PSR_IT_MASK | PSR_JAZELLE | PSR_BIG_ENDIAN | PSR_THUMB);

    regs->cpsr |= mode;
    regs->cpsr |= PSR_IRQ_MASK;
    if (mode == PSR_MODE_ABT) regs->cpsr |= PSR_ABT_MASK;
    if (sctlr & SCTLR_A32_ELx_TE) regs->cpsr |= PSR_THUMB;
    if (sctlr & SCTLR_Axx_ELx_EE) regs->cpsr |= PSR_BIG_ENDIAN;
}

static vaddr_t exception_handler32(vaddr_t offset) {
    register_t sctlr = READ_SYSREG(SCTLR_EL1);

    if (sctlr & SCTLR_A32_EL1_V)
        return 0xffff0000U + offset;
    else /* always have security exceptions */
        return READ_SYSREG(VBAR_EL1) + offset;
}

/* Injects an Undefined Instruction exception into the current vcpu,
 * PC is the exact address of the faulting instruction (without
 * pipeline adjustments). See TakeUndefInstrException pseudocode in
 * ARM ARM.
 */
static void inject_undef32_exception(struct cpu_user_regs *regs) {
    uint32_t spsr = regs->cpsr;
    int is_thumb = (regs->cpsr & PSR_THUMB);
    /* Saved PC points to the instruction past the faulting instruction. */
    uint32_t return_offset = is_thumb ? 2 : 4;

    BUG_ON(!is_32bit_domain(current->domain));

    /* Update processor mode */
    cpsr_switch_mode(regs, PSR_MODE_UND);

    /* Update banked registers */
    regs->spsr_und = spsr;
    regs->lr_und = regs->pc32 + return_offset;

    /* Branch to exception vector */
    regs->pc32 = exception_handler32(VECTOR32_UND);
}

/* Injects an Abort exception into the current vcpu, PC is the exact
 * address of the faulting instruction (without pipeline
 * adjustments). See TakePrefetchAbortException and
 * TakeDataAbortException pseudocode in ARM ARM.
 */
static void inject_abt32_exception(struct cpu_user_regs *regs, int prefetch, register_t addr) {
    uint32_t spsr = regs->cpsr;
    int is_thumb = (regs->cpsr & PSR_THUMB);
    /* Saved PC points to the instruction past the faulting instruction. */
    uint32_t return_offset = is_thumb ? 4 : 0;
    register_t fsr;

    BUG_ON(!is_32bit_domain(current->domain));

    cpsr_switch_mode(regs, PSR_MODE_ABT);

    /* Update banked registers */
    regs->spsr_abt = spsr;
    regs->lr_abt = regs->pc32 + return_offset;

    regs->pc32 = exception_handler32(prefetch ? VECTOR32_PABT : VECTOR32_DABT);

    /* Inject a debug fault, best we can do right now */
    if (READ_SYSREG(TCR_EL1) & TTBCR_EAE)
        fsr = FSR_LPAE | FSRL_STATUS_DEBUG;
    else
        fsr = FSRS_FS_DEBUG;

    if (prefetch) {
        /* Set IFAR and IFSR */
#ifdef CONFIG_ARM_32
        WRITE_SYSREG(addr, IFAR);
        WRITE_SYSREG(fsr, IFSR);
#else
        /* FAR_EL1[63:32] is AArch32 register IFAR */
        register_t far = READ_SYSREG(FAR_EL1) & 0xffffffffUL;
        far |= addr << 32;
        WRITE_SYSREG(far, FAR_EL1);
        WRITE_SYSREG(fsr, IFSR32_EL2);
#endif
    } else {
#ifdef CONFIG_ARM_32
        /* Set DFAR and DFSR */
        WRITE_SYSREG(addr, DFAR);
        WRITE_SYSREG(fsr, DFSR);
#else
        /* FAR_EL1[31:0] is AArch32 register DFAR */
        register_t far = READ_SYSREG(FAR_EL1) & ~0xffffffffUL;
        far |= addr;
        WRITE_SYSREG(far, FAR_EL1);
        /* ESR_EL1 is AArch32 register DFSR */
        WRITE_SYSREG(fsr, ESR_EL1);
#endif
    }
}

static void inject_dabt32_exception(struct cpu_user_regs *regs, register_t addr) {
    inject_abt32_exception(regs, 0, addr);
}

static void inject_pabt32_exception(struct cpu_user_regs *regs, register_t addr) {
    inject_abt32_exception(regs, 1, addr);
}

#ifdef CONFIG_ARM_64
/*
 * Take care to call this while regs contains the original faulting
 * state and not the (partially constructed) exception state.
 */
static vaddr_t exception_handler64(struct cpu_user_regs *regs, vaddr_t offset) {
    vaddr_t base = READ_SYSREG(VBAR_EL1);

    if (usr_mode(regs))
        base += VECTOR64_LOWER32_BASE;
    else if (psr_mode(regs->cpsr, PSR_MODE_EL0t))
        base += VECTOR64_LOWER64_BASE;
    else /* Otherwise must be from kernel mode */
        base += VECTOR64_CURRENT_SPx_BASE;

    return base + offset;
}

/* Inject an undefined exception into a 64 bit guest */
void inject_undef64_exception(struct cpu_user_regs *regs, int instr_len) {
    vaddr_t handler;
    const union hsr esr = {
        .iss = 0,
        .len = instr_len,
        .ec = HSR_EC_UNKNOWN,
    };

    BUG_ON(is_32bit_domain(current->domain));

    handler = exception_handler64(regs, VECTOR64_SYNC_OFFSET);

    regs->spsr_el1 = regs->cpsr;
    regs->elr_el1 = regs->pc;

    regs->cpsr = PSR_MODE_EL1h | PSR_ABT_MASK | PSR_FIQ_MASK | PSR_IRQ_MASK | PSR_DBG_MASK;
    regs->pc = handler;

    WRITE_SYSREG(esr.bits, ESR_EL1);
}

/* Inject an abort exception into a 64 bit guest */
static void inject_abt64_exception(struct cpu_user_regs *regs, int prefetch, register_t addr, int instr_len) {
    vaddr_t handler;
    union hsr esr = {
        .iss = 0,
        .len = instr_len,
    };

    if (regs_mode_is_user(regs))
        esr.ec = prefetch ? HSR_EC_INSTR_ABORT_LOWER_EL : HSR_EC_DATA_ABORT_LOWER_EL;
    else
        esr.ec = prefetch ? HSR_EC_INSTR_ABORT_CURR_EL : HSR_EC_DATA_ABORT_CURR_EL;

    BUG_ON(is_32bit_domain(current->domain));

    handler = exception_handler64(regs, VECTOR64_SYNC_OFFSET);

    regs->spsr_el1 = regs->cpsr;
    regs->elr_el1 = regs->pc;

    regs->cpsr = PSR_MODE_EL1h | PSR_ABT_MASK | PSR_FIQ_MASK | PSR_IRQ_MASK | PSR_DBG_MASK;
    regs->pc = handler;

    WRITE_SYSREG(addr, FAR_EL1);
    WRITE_SYSREG(esr.bits, ESR_EL1);
}

static void inject_dabt64_exception(struct cpu_user_regs *regs, register_t addr, int instr_len) {
    inject_abt64_exception(regs, 0, addr, instr_len);
}

static void inject_iabt64_exception(struct cpu_user_regs *regs, register_t addr, int instr_len) {
    inject_abt64_exception(regs, 1, addr, instr_len);
}

#endif

void inject_undef_exception(struct cpu_user_regs *regs, const union hsr hsr) {
    if (is_32bit_domain(current->domain)) inject_undef32_exception(regs);
#ifdef CONFIG_ARM_64
    else
        inject_undef64_exception(regs, hsr.len);
#endif
}

static void inject_iabt_exception(struct cpu_user_regs *regs, register_t addr, int instr_len) {
    if (is_32bit_domain(current->domain)) inject_pabt32_exception(regs, addr);
#ifdef CONFIG_ARM_64
    else
        inject_iabt64_exception(regs, addr, instr_len);
#endif
}

static void inject_dabt_exception(struct cpu_user_regs *regs, register_t addr, int instr_len) {
    if (is_32bit_domain(current->domain)) inject_dabt32_exception(regs, addr);
#ifdef CONFIG_ARM_64
    else
        inject_dabt64_exception(regs, addr, instr_len);
#endif
}

/*
 * Inject a virtual Abort/SError into the guest.
 *
 * This should only be called with 'current'.
 */
static void inject_vabt_exception(struct vcpu *v) {
    struct cpu_user_regs *regs = guest_cpu_user_regs();
    const union hsr hsr = {.bits = regs->hsr};

    ASSERT(v == current);

    /*
     * SVC/HVC/SMC already have an adjusted PC (See ARM ARM DDI 0487A.j
     * D1.10.1 for more details), which we need to correct in order to
     * return to after having injected the SError.
     */
    switch (hsr.ec) {
        case HSR_EC_SVC32:
        case HSR_EC_HVC32:
        case HSR_EC_SMC32:
#ifdef CONFIG_ARM_64
        case HSR_EC_SVC64:
        case HSR_EC_HVC64:
        case HSR_EC_SMC64:
#endif
            regs->pc -= hsr.len ? 4 : 2;
            break;

        default:
            break;
    }

    vcpu_hcr_set_flags(v, HCR_VA);
}

/*
 * SError exception handler.
 *
 * A true parameter "guest" means that the SError is type#1 or type#2.
 *
 * @guest indicates whether this is a SError generated by the guest.
 *
 * If true, the SError was generated by the guest, so it is safe to continue
 * and forward to the guest (if requested).
 *
 * If false, the SError was likely generated by the hypervisor. As we cannot
 * distinguish between precise and imprecise SErrors, it is not safe to
 * continue.
 *
 * Note that Arm32 asynchronous external abort generated by the
 * hypervisor will be handled in do_trap_data_abort().
 */
static void __do_trap_serror(struct cpu_user_regs *regs, bool guest) {
    /*
     * When using "DIVERSE", the SErrors generated by the guest will be
     * forwarded to the currently running vCPU.
     */
    if (serrors_op == SERRORS_DIVERSE && guest) return inject_vabt_exception(current);

    do_unexpected_trap("SError", regs);
}

struct reg_ctxt {
    /* Guest-side state */
    register_t sctlr_el1;
    register_t tcr_el1;
    uint64_t ttbr0_el1, ttbr1_el1;
#ifdef CONFIG_ARM_32
    uint32_t dfsr, ifsr;
    uint32_t dfar, ifar;
#else
    uint32_t esr_el1;
    uint64_t far;
    uint32_t ifsr32_el2;
#endif

    /* Hypervisor-side state */
    uint64_t vttbr_el2;
};

static const char *mode_string(register_t cpsr) {
    uint32_t mode;
    static const char *mode_strings[] = {
        [PSR_MODE_USR] = "32-bit Guest USR",
        [PSR_MODE_FIQ] = "32-bit Guest FIQ",
        [PSR_MODE_IRQ] = "32-bit Guest IRQ",
        [PSR_MODE_SVC] = "32-bit Guest SVC",
        [PSR_MODE_MON] = "32-bit Monitor",
        [PSR_MODE_ABT] = "32-bit Guest ABT",
        [PSR_MODE_HYP] = "Hypervisor",
        [PSR_MODE_UND] = "32-bit Guest UND",
        [PSR_MODE_SYS] = "32-bit Guest SYS",
#ifdef CONFIG_ARM_64
        [PSR_MODE_EL3h] = "64-bit EL3h (Monitor, handler)",
        [PSR_MODE_EL3t] = "64-bit EL3t (Monitor, thread)",
        [PSR_MODE_EL2h] = "64-bit EL2h (Hypervisor, handler)",
        [PSR_MODE_EL2t] = "64-bit EL2t (Hypervisor, thread)",
        [PSR_MODE_EL1h] = "64-bit EL1h (Guest Kernel, handler)",
        [PSR_MODE_EL1t] = "64-bit EL1t (Guest Kernel, thread)",
        [PSR_MODE_EL0t] = "64-bit EL0t (Guest User)",
#endif
    };
    mode = cpsr & PSR_MODE_MASK;

    if (mode >= ARRAY_SIZE(mode_strings)) return "Unknown";
    return mode_strings[mode] ?: "Unknown";
}

static void show_registers_32(const struct cpu_user_regs *regs, const struct reg_ctxt *ctxt, bool guest_mode_on, const struct vcpu *v) {
#ifdef CONFIG_ARM_64
    BUG_ON(!(regs->cpsr & PSR_MODE_BIT));
    printk("PC:     %08" PRIx32 "\n", regs->pc32);
#else
    printk("PC:     %08" PRIx32, regs->pc);
    if (!guest_mode_on) printk(" %pS", _p(regs->pc));
    printk("\n");
#endif
    printk("CPSR:   %" PRIregister " MODE:%s\n", regs->cpsr, mode_string(regs->cpsr));
    printk("     R0: %08" PRIx32 " R1: %08" PRIx32 " R2: %08" PRIx32 " R3: %08" PRIx32 "\n", regs->r0, regs->r1, regs->r2, regs->r3);
    printk("     R4: %08" PRIx32 " R5: %08" PRIx32 " R6: %08" PRIx32 " R7: %08" PRIx32 "\n", regs->r4, regs->r5, regs->r6, regs->r7);
    printk("     R8: %08" PRIx32 " R9: %08" PRIx32 " R10:%08" PRIx32 " R11:%08" PRIx32 " R12:%08" PRIx32 "\n", regs->r8, regs->r9, regs->r10,
#ifdef CONFIG_ARM_64
           regs->r11,
#else
           regs->fp,
#endif
           regs->r12);

    if (guest_mode_on) {
        printk("USR: SP: %08" PRIx32 " LR: %" PRIregister "\n", regs->sp_usr, regs->lr);
        printk("SVC: SP: %08" PRIx32 " LR: %08" PRIx32 " SPSR:%08" PRIx32 "\n", regs->sp_svc, regs->lr_svc, regs->spsr_svc);
        printk("ABT: SP: %08" PRIx32 " LR: %08" PRIx32 " SPSR:%08" PRIx32 "\n", regs->sp_abt, regs->lr_abt, regs->spsr_abt);
        printk("UND: SP: %08" PRIx32 " LR: %08" PRIx32 " SPSR:%08" PRIx32 "\n", regs->sp_und, regs->lr_und, regs->spsr_und);
        printk("IRQ: SP: %08" PRIx32 " LR: %08" PRIx32 " SPSR:%08" PRIx32 "\n", regs->sp_irq, regs->lr_irq, regs->spsr_irq);
        printk("FIQ: SP: %08" PRIx32 " LR: %08" PRIx32 " SPSR:%08" PRIx32 "\n", regs->sp_fiq, regs->lr_fiq, regs->spsr_fiq);
        printk("FIQ: R8: %08" PRIx32 " R9: %08" PRIx32 " R10:%08" PRIx32 " R11:%08" PRIx32 " R12:%08" PRIx32 "\n", regs->r8_fiq, regs->r9_fiq, regs->r10_fiq,
               regs->r11_fiq, regs->r11_fiq);
    }
#ifndef CONFIG_ARM_64
    else {
        printk("HYP: SP: %08" PRIx32 " LR: %" PRIregister "\n", regs->sp, regs->lr);
    }
#endif
    printk("\n");

    if (guest_mode_on) {
        printk("     SCTLR: %" PRIregister "\n", ctxt->sctlr_el1);
        printk("       TCR: %" PRIregister "\n", ctxt->tcr_el1);
        printk("     TTBR0: %016" PRIx64 "\n", ctxt->ttbr0_el1);
        printk("     TTBR1: %016" PRIx64 "\n", ctxt->ttbr1_el1);
        printk("      IFAR: %08" PRIx32 ", IFSR: %08" PRIx32 "\n"
               "      DFAR: %08" PRIx32 ", DFSR: %08" PRIx32 "\n",
#ifdef CONFIG_ARM_64
               (uint32_t)(ctxt->far >> 32), ctxt->ifsr32_el2, (uint32_t)(ctxt->far & 0xffffffffU), ctxt->esr_el1
#else
               ctxt->ifar, ctxt->ifsr, ctxt->dfar, ctxt->dfsr
#endif
        );
        printk("\n");
    }
}

#ifdef CONFIG_ARM_64
static void show_registers_64(const struct cpu_user_regs *regs, const struct reg_ctxt *ctxt, bool guest_mode_on, const struct vcpu *v) {
    BUG_ON((regs->cpsr & PSR_MODE_BIT));

    printk("PC:     %016" PRIx64, regs->pc);
    if (!guest_mode_on) printk(" %pS", _p(regs->pc));
    printk("\n");
    printk("LR:     %016" PRIx64 "\n", regs->lr);
    if (guest_mode_on) {
        printk("SP_EL0: %016" PRIx64 "\n", regs->sp_el0);
        printk("SP_EL1: %016" PRIx64 "\n", regs->sp_el1);
    } else {
        printk("SP:     %016" PRIx64 "\n", regs->sp);
    }
    printk("CPSR:   %016" PRIx64 " MODE:%s\n", regs->cpsr, mode_string(regs->cpsr));
    printk("     X0: %016" PRIx64 "  X1: %016" PRIx64 "  X2: %016" PRIx64 "\n", regs->x0, regs->x1, regs->x2);
    printk("     X3: %016" PRIx64 "  X4: %016" PRIx64 "  X5: %016" PRIx64 "\n", regs->x3, regs->x4, regs->x5);
    printk("     X6: %016" PRIx64 "  X7: %016" PRIx64 "  X8: %016" PRIx64 "\n", regs->x6, regs->x7, regs->x8);
    printk("     X9: %016" PRIx64 " X10: %016" PRIx64 " X11: %016" PRIx64 "\n", regs->x9, regs->x10, regs->x11);
    printk("    X12: %016" PRIx64 " X13: %016" PRIx64 " X14: %016" PRIx64 "\n", regs->x12, regs->x13, regs->x14);
    printk("    X15: %016" PRIx64 " X16: %016" PRIx64 " X17: %016" PRIx64 "\n", regs->x15, regs->x16, regs->x17);
    printk("    X18: %016" PRIx64 " X19: %016" PRIx64 " X20: %016" PRIx64 "\n", regs->x18, regs->x19, regs->x20);
    printk("    X21: %016" PRIx64 " X22: %016" PRIx64 " X23: %016" PRIx64 "\n", regs->x21, regs->x22, regs->x23);
    printk("    X24: %016" PRIx64 " X25: %016" PRIx64 " X26: %016" PRIx64 "\n", regs->x24, regs->x25, regs->x26);
    printk("    X27: %016" PRIx64 " X28: %016" PRIx64 "  FP: %016" PRIx64 "\n", regs->x27, regs->x28, regs->fp);
    printk("\n");

    if (guest_mode_on) {
        printk("   ELR_EL1: %016" PRIx64 "\n", regs->elr_el1);
        printk("   ESR_EL1: %08" PRIx32 "\n", ctxt->esr_el1);
        printk("   FAR_EL1: %016" PRIx64 "\n", ctxt->far);
        printk("\n");
        printk(" SCTLR_EL1: %" PRIregister "\n", ctxt->sctlr_el1);
        printk("   TCR_EL1: %" PRIregister "\n", ctxt->tcr_el1);
        printk(" TTBR0_EL1: %016" PRIx64 "\n", ctxt->ttbr0_el1);
        printk(" TTBR1_EL1: %016" PRIx64 "\n", ctxt->ttbr1_el1);
        printk("\n");
    }
}
#endif

static void _show_registers(const struct cpu_user_regs *regs, const struct reg_ctxt *ctxt, bool guest_mode_on, const struct vcpu *v) {
    print_prtos_info();

    printk("CPU:    %d\n", smp_processor_id());

    if (guest_mode_on) {
        if (regs_mode_is_32bit(regs)) show_registers_32(regs, ctxt, guest_mode_on, v);
#ifdef CONFIG_ARM_64
        else
            show_registers_64(regs, ctxt, guest_mode_on, v);
#endif
    } else {
#ifdef CONFIG_ARM_64
        show_registers_64(regs, ctxt, guest_mode_on, v);
#else
        show_registers_32(regs, ctxt, guest_mode_on, v);
#endif
    }
    printk("  VTCR_EL2: %" PRIregister "\n", READ_SYSREG(VTCR_EL2));
    printk(" VTTBR_EL2: %016" PRIx64 "\n", ctxt->vttbr_el2);
    printk("\n");

    printk(" SCTLR_EL2: %" PRIregister "\n", READ_SYSREG(SCTLR_EL2));
    printk("   HCR_EL2: %" PRIregister "\n", READ_SYSREG(HCR_EL2));
    printk(" TTBR0_EL2: %016" PRIx64 "\n", READ_SYSREG64(TTBR0_EL2));
    printk("\n");
    printk("   ESR_EL2: %" PRIregister "\n", regs->hsr);
    printk(" HPFAR_EL2: %" PRIregister "\n", READ_SYSREG(HPFAR_EL2));

#ifdef CONFIG_ARM_32
    printk("     HDFAR: %08" PRIx32 "\n", READ_CP32(HDFAR));
    printk("     HIFAR: %08" PRIx32 "\n", READ_CP32(HIFAR));
#else
    printk("   FAR_EL2: %016" PRIx64 "\n", READ_SYSREG64(FAR_EL2));
#endif
    printk("\n");
}

void show_registers(const struct cpu_user_regs *regs) {
    struct reg_ctxt ctxt;
    ctxt.sctlr_el1 = READ_SYSREG(SCTLR_EL1);
    ctxt.tcr_el1 = READ_SYSREG(TCR_EL1);
    ctxt.ttbr0_el1 = READ_SYSREG64(TTBR0_EL1);
    ctxt.ttbr1_el1 = READ_SYSREG64(TTBR1_EL1);
#ifdef CONFIG_ARM_32
    ctxt.dfar = READ_CP32(DFAR);
    ctxt.ifar = READ_CP32(IFAR);
    ctxt.dfsr = READ_CP32(DFSR);
    ctxt.ifsr = READ_CP32(IFSR);
#else
    ctxt.far = READ_SYSREG(FAR_EL1);
    ctxt.esr_el1 = READ_SYSREG(ESR_EL1);
    if (guest_mode(regs) && is_32bit_domain(current->domain)) ctxt.ifsr32_el2 = READ_SYSREG(IFSR32_EL2);
#endif
    ctxt.vttbr_el2 = READ_SYSREG64(VTTBR_EL2);

    _show_registers(regs, &ctxt, guest_mode(regs), current);
}

void vcpu_show_registers(const struct vcpu *v) {
    struct reg_ctxt ctxt;
    ctxt.sctlr_el1 = v->arch.sctlr;
    ctxt.tcr_el1 = v->arch.ttbcr;
    ctxt.ttbr0_el1 = v->arch.ttbr0;
    ctxt.ttbr1_el1 = v->arch.ttbr1;
#ifdef CONFIG_ARM_32
    ctxt.dfar = v->arch.dfar;
    ctxt.ifar = v->arch.ifar;
    ctxt.dfsr = v->arch.dfsr;
    ctxt.ifsr = v->arch.ifsr;
#else
    ctxt.far = v->arch.far;
    ctxt.esr_el1 = v->arch.esr;
    ctxt.ifsr32_el2 = v->arch.ifsr;
#endif

    ctxt.vttbr_el2 = v->domain->arch.p2m.vttbr;

    _show_registers(&v->arch.cpu_info->guest_cpu_user_regs, &ctxt, 1, v);
}

static void show_guest_stack(struct vcpu *v, const struct cpu_user_regs *regs) {
    int i;
    vaddr_t sp;
    struct page_info *page;
    void *mapped;
    unsigned long *stack, addr;

    if (test_bit(_VPF_down, &v->pause_flags)) {
        printk("No stack trace, VCPU offline\n");
        return;
    }

    switch (regs->cpsr & PSR_MODE_MASK) {
        case PSR_MODE_USR:
        case PSR_MODE_SYS:
#ifdef CONFIG_ARM_64
        case PSR_MODE_EL0t:
#endif
            printk("No stack trace for guest user-mode\n");
            return;

        case PSR_MODE_FIQ:
            sp = regs->sp_fiq;
            break;
        case PSR_MODE_IRQ:
            sp = regs->sp_irq;
            break;
        case PSR_MODE_SVC:
            sp = regs->sp_svc;
            break;
        case PSR_MODE_ABT:
            sp = regs->sp_abt;
            break;
        case PSR_MODE_UND:
            sp = regs->sp_und;
            break;

#ifdef CONFIG_ARM_64
        case PSR_MODE_EL1t:
            sp = regs->sp_el0;
            break;
        case PSR_MODE_EL1h:
            sp = regs->sp_el1;
            break;
#endif

        case PSR_MODE_HYP:
        case PSR_MODE_MON:
#ifdef CONFIG_ARM_64
        case PSR_MODE_EL3h:
        case PSR_MODE_EL3t:
        case PSR_MODE_EL2h:
        case PSR_MODE_EL2t:
#endif
        default:
            BUG();
            return;
    }

    printk("Guest stack trace from sp=%" PRIvaddr ":\n  ", sp);

    if (sp & (sizeof(long) - 1)) {
        printk("Stack is misaligned\n");
        return;
    }

    page = get_page_from_gva(v, sp, GV2M_READ);
    if (page == NULL) {
        printk("Failed to convert stack to physical address\n");
        return;
    }

    mapped = __map_domain_page(page);

    stack = mapped + (sp & ~PAGE_MASK);

    for (i = 0; i < (debug_stack_lines * stack_words_per_line); i++) {
        if ((((long)stack - 1) ^ ((long)(stack + 1) - 1)) & PAGE_SIZE) break;
        addr = *stack;
        if ((i != 0) && ((i % stack_words_per_line) == 0)) printk("\n  ");
        printk(" %p", _p(addr));
        stack++;
    }
    if (i == 0) printk("Stack empty.");
    printk("\n");
    unmap_domain_page(mapped);
    put_page(page);
}

#define STACK_BEFORE_EXCEPTION(regs) ((register_t *)(regs)->sp)
#ifdef CONFIG_ARM_32
/* Frame pointer points to the return address:
 * (largest address)
 * | cpu_info
 * | [...]                                   |
 * | return addr      <-----------------,    |
 * | fp --------------------------------+----'
 * | [...]                              |
 * | return addr      <------------,    |
 * | fp ---------------------------+----'
 * | [...]                         |
 * | return addr      <- regs->fp  |
 * | fp ---------------------------'
 * |
 * v (smallest address, sp)
 */
#define STACK_FRAME_BASE(fp) ((register_t *)(fp) - 1)
#else
/* Frame pointer points to the next frame:
 * (largest address)
 * | cpu_info
 * | [...]                                   |
 * | return addr                             |
 * | fp <-------------------------------, >--'
 * | [...]                              |
 * | return addr                        |
 * | fp <--------------------------, >--'
 * | [...]                         |
 * | return addr      <- regs->fp  |
 * | fp ---------------------------'
 * |
 * v (smallest address, sp)
 */
#define STACK_FRAME_BASE(fp) ((register_t *)(fp))
#endif
static void show_trace(const struct cpu_user_regs *regs) {
    register_t *frame, next, addr, low, high;

    printk("PRTOS call trace:\n");

    printk("   [<%p>] %pS (PC)\n", _p(regs->pc), _p(regs->pc));
    printk("   [<%p>] %pS (LR)\n", _p(regs->lr), _p(regs->lr));

    /* Bounds for range of valid frame pointer. */
    low = (register_t)(STACK_BEFORE_EXCEPTION(regs));
    high = (low & ~(STACK_SIZE - 1)) + (STACK_SIZE - sizeof(struct cpu_info));

    /* The initial frame pointer. */
    next = regs->fp;

    for (;;) {
        if ((next < low) || (next >= high)) break;

        /* Ordinary stack frame. */
        frame = STACK_FRAME_BASE(next);
        next = frame[0];
        addr = frame[1];

        printk("   [<%p>] %pS\n", _p(addr), _p(addr));

        low = (register_t)&frame[1];
    }

    printk("\n");
}

void show_stack(const struct cpu_user_regs *regs) {
    register_t *stack = STACK_BEFORE_EXCEPTION(regs), addr;
    int i;

    if (guest_mode(regs)) return show_guest_stack(current, regs);

    printk("PRTOS stack trace from sp=%p:\n  ", stack);

    for (i = 0; i < (debug_stack_lines * stack_words_per_line); i++) {
        if (((long)stack & (STACK_SIZE - BYTES_PER_LONG)) == 0) break;
        if ((i != 0) && ((i % stack_words_per_line) == 0)) printk("\n  ");

        addr = *stack++;
        printk(" %p", _p(addr));
    }
    if (i == 0) printk("Stack empty.");
    printk("\n");

    show_trace(regs);
}

void show_execution_state(const struct cpu_user_regs *regs) {
    show_registers(regs);
    show_stack(regs);
}

void vcpu_show_execution_state(struct vcpu *v) {
    printk("*** Dumping Dom%d vcpu#%d state: ***\n", v->domain->domain_id, v->vcpu_id);

    if (v == current) {
        show_execution_state(guest_cpu_user_regs());
        return;
    }

    vcpu_pause(v); /* acceptably dangerous */

    vcpu_show_registers(v);
    if (!regs_mode_is_user(&v->arch.cpu_info->guest_cpu_user_regs)) show_guest_stack(v, &v->arch.cpu_info->guest_cpu_user_regs);

    vcpu_unpause(v);
}

void do_unexpected_trap(const char *msg, const struct cpu_user_regs *regs) {
    printk("CPU%d: Unexpected Trap: %s\n", smp_processor_id(), msg);
    show_execution_state(regs);
    panic("CPU%d: Unexpected Trap: %s\n", smp_processor_id(), msg);
}

int do_bug_frame(const struct cpu_user_regs *regs, vaddr_t pc) {
    const struct bug_frame *bug = NULL;
    const char *prefix = "", *filename, *predicate;
    unsigned long fixup;
    int id = -1, lineno;
    const struct virtual_region *region;

    region = find_text_region(pc);
    if (region) {
        for (id = 0; id < BUGFRAME_NR; id++) {
            const struct bug_frame *b;

            for (b = region->frame[id].start; b < region->frame[id].stop; b++) {
                if (((vaddr_t)bug_loc(b)) == pc) {
                    bug = b;
                    goto found;
                }
            }
        }
    }
found:
    if (!bug) return -ENOENT;

    if (id == BUGFRAME_run_fn) {
        bug_fn_t *fn = (void *)regs->BUG_FN_REG;

        fn(regs);
        return 0;
    }

    /* WARN, BUG or ASSERT: decode the filename pointer and line number. */
    filename = bug_file(bug);
    if (!is_kernel(filename)) return -EINVAL;
    fixup = strlen(filename);
    if (fixup > 50) {
        filename += fixup - 47;
        prefix = "...";
    }
    lineno = bug_line(bug);

    switch (id) {
        case BUGFRAME_warn:
            printk("PRTOS WARN at %s%s:%d\n", prefix, filename, lineno);
            show_execution_state(regs);
            return 0;

        case BUGFRAME_bug:
            printk("PRTOS BUG at %s%s:%d\n", prefix, filename, lineno);
            show_execution_state(regs);
            panic("PRTOS BUG at %s%s:%d\n", prefix, filename, lineno);

        case BUGFRAME_assert:
            /* ASSERT: decode the predicate string pointer. */
            predicate = bug_msg(bug);
            if (!is_kernel(predicate)) predicate = "<unknown>";

            printk("Assertion '%s' failed at %s%s:%d\n", predicate, prefix, filename, lineno);
            show_execution_state(regs);
            panic("Assertion '%s' failed at %s%s:%d\n", predicate, prefix, filename, lineno);
    }

    return -EINVAL;
}

#ifdef CONFIG_ARM_64
static void do_trap_brk(struct cpu_user_regs *regs, const union hsr hsr) {
    /*
     * HCR_EL2.TGE and MDCR_EL2.TDR are currently not set. So we should
     * never receive software breakpoing exception for EL1 and EL0 here.
     */
    if (!hyp_mode(regs)) {
        domain_crash(current->domain);
        return;
    }

    switch (hsr.brk.comment) {
        case BRK_BUG_FRAME_IMM:
            if (do_bug_frame(regs, regs->pc)) goto die;

            regs->pc += 4;

            break;

        default:
        die:
            do_unexpected_trap("Undefined Breakpoint Value", regs);
    }
}
#endif

static register_t do_deprecated_hypercall(void) {
    struct cpu_user_regs *regs = guest_cpu_user_regs();
    const register_t op =
#ifdef CONFIG_ARM_64
        !is_32bit_domain(current->domain) ? regs->x16 :
#endif
                                          regs->r12;

    gdprintk(PRTOSLOG_DEBUG, "%pv: deprecated hypercall %lu\n", current, (unsigned long)op);
    return -ENOSYS;
}

long dep_sched_op_compat(int cmd, unsigned long arg) {
    return do_deprecated_hypercall();
}

long dep_event_channel_op_compat(PRTOS_GUEST_HANDLE_PARAM(evtchn_op_t) uop) {
    return do_deprecated_hypercall();
}

long dep_physdev_op_compat(PRTOS_GUEST_HANDLE_PARAM(physdev_op_t) uop) {
    return do_deprecated_hypercall();
}

#ifndef NDEBUG
static void do_debug_trap(struct cpu_user_regs *regs, unsigned int code) {
    uint32_t reg;
    uint32_t domid = current->domain->domain_id;

    switch (code) {
        case 0xe0 ... 0xef:
            reg = code - 0xe0;
            printk("DOM%d: R%d = 0x%" PRIregister " at 0x%" PRIvaddr "\n", domid, reg, get_user_reg(regs, reg), regs->pc);
            break;
        case 0xfd:
            printk("DOM%d: Reached %" PRIvaddr "\n", domid, regs->pc);
            break;
        case 0xfe:
            printk("%c", (char)(get_user_reg(regs, 0) & 0xff));
            break;
        case 0xff:
            printk("DOM%d: DEBUG\n", domid);
            show_execution_state(regs);
            break;
        default:
            printk("DOM%d: Unhandled debug trap %#x\n", domid, code);
            break;
    }
}
#endif

#ifdef CONFIG_ARM_64
#define HYPERCALL_RESULT_REG(r) (r)->x0
#define HYPERCALL_ARG1(r) (r)->x0
#define HYPERCALL_ARG2(r) (r)->x1
#define HYPERCALL_ARG3(r) (r)->x2
#define HYPERCALL_ARG4(r) (r)->x3
#define HYPERCALL_ARG5(r) (r)->x4
#else
#define HYPERCALL_RESULT_REG(r) (r)->r0
#define HYPERCALL_ARG1(r) (r)->r0
#define HYPERCALL_ARG2(r) (r)->r1
#define HYPERCALL_ARG3(r) (r)->r2
#define HYPERCALL_ARG4(r) (r)->r3
#define HYPERCALL_ARG5(r) (r)->r4
#endif

static const unsigned char hypercall_args[] = hypercall_args_arm;

static void do_trap_hypercall(struct cpu_user_regs *regs, register_t *nr, const union hsr hsr) {
    struct vcpu *curr = current;

    if (hsr.iss != PRTOS_HYPERCALL_TAG) {
        gprintk(PRTOSLOG_WARNING, "Invalid HVC imm 0x%x\n", hsr.iss);
        return inject_undef_exception(regs, hsr);
    }

    curr->hcall_preempted = false;

    perfc_incra(hypercalls, *nr);

    call_handlers_arm(*nr, HYPERCALL_RESULT_REG(regs), HYPERCALL_ARG1(regs), HYPERCALL_ARG2(regs), HYPERCALL_ARG3(regs), HYPERCALL_ARG4(regs),
                      HYPERCALL_ARG5(regs));

#ifndef NDEBUG
    if (!curr->hcall_preempted && HYPERCALL_RESULT_REG(regs) != -ENOSYS) {
        /* Deliberately corrupt parameter regs used by this hypercall. */
        switch (hypercall_args[*nr]) {
            case 5:
                HYPERCALL_ARG5(regs) = 0xDEADBEEFU;
                fallthrough;
            case 4:
                HYPERCALL_ARG4(regs) = 0xDEADBEEFU;
                fallthrough;
            case 3:
                HYPERCALL_ARG3(regs) = 0xDEADBEEFU;
                fallthrough;
            case 2:
                HYPERCALL_ARG2(regs) = 0xDEADBEEFU;
                fallthrough;
            case 1: /* Don't clobber x0/r0 -- it's the return value */
            case 0: /* -ENOSYS case */
                break;
            default:
                BUG();
        }
        *nr = 0xDEADBEEFU;
    }
#endif

    /* Ensure the hypercall trap instruction is re-executed. */
    if (curr->hcall_preempted) regs->pc -= 4; /* re-execute 'hvc #PRTOS_HYPERCALL_TAG' */

#ifdef CONFIG_IOREQ_SERVER
    /*
     * We call ioreq_signal_mapcache_invalidate from do_trap_hypercall()
     * because the only way a guest can modify its P2M on Arm is via an
     * hypercall.
     * Note that sending the invalidation request causes the vCPU to block
     * until all the IOREQ servers have acknowledged the invalidation.
     */
    if (unlikely(curr->mapcache_invalidate) && test_and_clear_bool(curr->mapcache_invalidate)) ioreq_signal_mapcache_invalidate();
#endif
}

void arch_hypercall_tasklet_result(struct vcpu *v, long res) {
    struct cpu_user_regs *regs = &v->arch.cpu_info->guest_cpu_user_regs;

    HYPERCALL_RESULT_REG(regs) = res;
}

static bool check_multicall_32bit_clean(struct multicall_entry *multi) {
    int i;

    for (i = 0; i < hypercall_args[multi->op]; i++) {
        if (unlikely(multi->args[i] & 0xffffffff00000000ULL)) {
            printk("%pv: multicall argument %d is not 32-bit clean %" PRIx64 "\n", current, i, multi->args[i]);
            domain_crash(current->domain);
            return false;
        }
    }

    return true;
}

enum mc_disposition arch_do_multicall_call(struct mc_state *mcs) {
    struct multicall_entry *multi = &mcs->call;

    if (multi->op >= ARRAY_SIZE(hypercall_args)) {
        multi->result = -ENOSYS;
        return mc_continue;
    }

    if (is_32bit_domain(current->domain) && !check_multicall_32bit_clean(multi)) return mc_continue;

    call_handlers_arm(multi->op, multi->result, multi->args[0], multi->args[1], multi->args[2], multi->args[3], multi->args[4]);

    return likely(!regs_mode_is_user(guest_cpu_user_regs())) ? mc_continue : mc_preempt;
}

/*
 * stolen from arch/arm/kernel/opcodes.c
 *
 * condition code lookup table
 * index into the table is test code: EQ, NE, ... LT, GT, AL, NV
 *
 * bit position in short is condition code: NZCV
 */
static const unsigned short cc_map[16] = {
    0xF0F0, /* EQ == Z set            */
    0x0F0F, /* NE                     */
    0xCCCC, /* CS == C set            */
    0x3333, /* CC                     */
    0xFF00, /* MI == N set            */
    0x00FF, /* PL                     */
    0xAAAA, /* VS == V set            */
    0x5555, /* VC                     */
    0x0C0C, /* HI == C set && Z clear */
    0xF3F3, /* LS == C clear || Z set */
    0xAA55, /* GE == (N==V)           */
    0x55AA, /* LT == (N!=V)           */
    0x0A05, /* GT == (!Z && (N==V))   */
    0xF5FA, /* LE == (Z || (N!=V))    */
    0xFFFF, /* AL always              */
    0       /* NV                     */
};

int check_conditional_instr(struct cpu_user_regs *regs, const union hsr hsr) {
    register_t cpsr, cpsr_cond;
    int cond;

    /*
     * SMC32 instruction case is special. Under SMC32 we mean SMC
     * instruction on ARMv7 or SMC instruction originating from
     * AArch32 state on ARMv8.
     * On ARMv7 it will be trapped only if it passed condition check
     * (ARM DDI 0406C.c page B3-1431), but we need to check condition
     * flags on ARMv8 (ARM DDI 0487B.a page D7-2271).
     * Encoding for HSR.ISS on ARMv8 is backwards compatible with ARMv7:
     * HSR.ISS is defined as UNK/SBZP on ARMv7 which means, that it
     * will be read as 0. This includes CCKNOWNPASS field.
     * If CCKNOWNPASS == 0 then this was an unconditional instruction or
     * it has passed conditional check (ARM DDI 0487B.a page D7-2272).
     */
    if (hsr.ec == HSR_EC_SMC32 && hsr.smc32.ccknownpass == 0) return 1;

    /* Unconditional Exception classes */
    if (hsr.ec == HSR_EC_UNKNOWN || (hsr.ec >= 0x10 && hsr.ec != HSR_EC_SMC32)) return 1;

    /* Check for valid condition in hsr */
    cond = hsr.cond.ccvalid ? hsr.cond.cc : -1;

    /* Unconditional instruction */
    if (cond == 0xe) return 1;

    cpsr = regs->cpsr;

    /* If cc is not valid then we need to examine the IT state */
    if (cond < 0) {
        unsigned long it;

        BUG_ON(!regs_mode_is_32bit(regs) || !(cpsr & PSR_THUMB));

        it = ((cpsr >> (10 - 2)) & 0xfc) | ((cpsr >> 25) & 0x3);

        /* it == 0 => unconditional. */
        if (it == 0) return 1;

        /* The cond for this instruction works out as the top 4 bits. */
        cond = (it >> 4);
    }

    cpsr_cond = cpsr >> 28;

    if (!((cc_map[cond] >> cpsr_cond) & 1)) {
        perfc_incr(trap_uncond);
        return 0;
    }
    return 1;
}

void advance_pc(struct cpu_user_regs *regs, const union hsr hsr) {
    register_t itbits, cond, cpsr = regs->cpsr;
    bool is_thumb = regs_mode_is_32bit(regs) && (cpsr & PSR_THUMB);

    if (is_thumb && (cpsr & PSR_IT_MASK)) {
        /* The ITSTATE[7:0] block is contained in CPSR[15:10],CPSR[26:25]
         *
         * ITSTATE[7:5] are the condition code
         * ITSTATE[4:0] are the IT bits
         *
         * If the condition is non-zero then the IT state machine is
         * advanced by shifting the IT bits left.
         *
         * See A2-51 and B1-1148 of DDI 0406C.b.
         */
        cond = (cpsr & 0xe000) >> 13;
        itbits = (cpsr & 0x1c00) >> (10 - 2);
        itbits |= (cpsr & (0x3 << 25)) >> 25;

        if ((itbits & 0x7) == 0)
            itbits = cond = 0;
        else
            itbits = (itbits << 1) & 0x1f;

        cpsr &= ~PSR_IT_MASK;
        cpsr |= cond << 13;
        cpsr |= (itbits & 0x1c) << (10 - 2);
        cpsr |= (itbits & 0x3) << 25;

        regs->cpsr = cpsr;
    }

    regs->pc += hsr.len ? 4 : 2;
}

/* Read as zero and write ignore */
void handle_raz_wi(struct cpu_user_regs *regs, int regidx, bool read, const union hsr hsr, int min_el) {
    ASSERT((min_el == 0) || (min_el == 1));

    if (min_el > 0 && regs_mode_is_user(regs)) return inject_undef_exception(regs, hsr);

    if (read) set_user_reg(regs, regidx, 0);
    /* else: write ignored */

    advance_pc(regs, hsr);
}

/* write only as write ignore */
void handle_wo_wi(struct cpu_user_regs *regs, int regidx, bool read, const union hsr hsr, int min_el) {
    ASSERT((min_el == 0) || (min_el == 1));

    if (min_el > 0 && regs_mode_is_user(regs)) return inject_undef_exception(regs, hsr);

    if (read) return inject_undef_exception(regs, hsr);
    /* else: ignore */

    advance_pc(regs, hsr);
}

/* Read only as value provided with 'val' argument of this function */
void handle_ro_read_val(struct cpu_user_regs *regs, int regidx, bool read, const union hsr hsr, int min_el, register_t val) {
    ASSERT((min_el == 0) || (min_el == 1));

    if (min_el > 0 && regs_mode_is_user(regs)) return inject_undef_exception(regs, hsr);

    if (!read) return inject_undef_exception(regs, hsr);

    set_user_reg(regs, regidx, val);

    advance_pc(regs, hsr);
}

/* Read only as read as zero */
void handle_ro_raz(struct cpu_user_regs *regs, int regidx, bool read, const union hsr hsr, int min_el) {
    handle_ro_read_val(regs, regidx, read, hsr, min_el, 0);
}

/*
 * Return the value of the hypervisor fault address register.
 *
 * On ARM32, the register will be different depending whether the
 * fault is a prefetch abort or data abort.
 */
static inline vaddr_t get_hfar(bool is_data) {
    vaddr_t gva;

#ifdef CONFIG_ARM_32
    if (is_data)
        gva = READ_CP32(HDFAR);
    else
        gva = READ_CP32(HIFAR);
#else
    gva = READ_SYSREG(FAR_EL2);
#endif

    return gva;
}

static inline paddr_t get_faulting_ipa(vaddr_t gva) {
    register_t hpfar = READ_SYSREG(HPFAR_EL2);
    paddr_t ipa;

    ipa = (paddr_t)(hpfar & HPFAR_MASK) << (12 - 4);
    ipa |= gva & ~PAGE_MASK;

    return ipa;
}

static inline bool hpfar_is_valid(bool s1ptw, uint8_t fsc) {
    /*
     * HPFAR is valid if one of the following cases are true:
     *  1. the stage 2 fault happen during a stage 1 page table walk
     *  (the bit ESR_EL2.S1PTW is set)
     *  2. the fault was due to a translation fault and the processor
     *  does not carry erratum #834220
     *
     * Note that technically HPFAR is valid for other cases, but they
     * are currently not supported by PRTOS.
     */
    return s1ptw || (fsc == FSC_FLT_TRANS && !check_workaround_834220());
}

/*
 * Try to map the MMIO regions for some special cases:
 * 1. When using ACPI, most of the MMIO regions will be mapped on-demand
 *    in stage-2 page tables for the hardware domain because PRTOS is not
 *    able to know from the EFI memory map the MMIO regions.
 * 2. For guests using GICv2, the GICv2 CPU interface mapping is created
 *    on the first access of the MMIO region.
 */
static bool try_map_mmio(gfn_t gfn) {
    struct domain *d = current->domain;

    /* For the hardware domain, all MMIOs are mapped with GFN == MFN */
    mfn_t mfn = _mfn(gfn_x(gfn));

    /*
     * Map the GICv2 virtual CPU interface in the GIC CPU interface
     * region of the guest on the first access of the MMIO region.
     */
    if (d->arch.vgic.version == GIC_V2 && gfn_to_gaddr(gfn) >= d->arch.vgic.cbase && (gfn_to_gaddr(gfn) - d->arch.vgic.cbase) < d->arch.vgic.csize)
        return !map_mmio_regions(d, gfn, d->arch.vgic.csize / PAGE_SIZE, maddr_to_mfn(d->arch.vgic.vbase));

    /*
     * Device-Tree should already have everything mapped when building
     * the hardware domain.
     */
    if (acpi_disabled) return false;

    if (!is_hardware_domain(d)) return false;

    /* The hardware domain can only map permitted MMIO regions */
    if (!iomem_access_permitted(d, mfn_x(mfn), mfn_x(mfn))) return false;

    return !map_regions_p2mt(d, gfn, 1, mfn, p2m_mmio_direct_c);
}

static inline bool check_p2m(bool is_data, paddr_t gpa) {
    /*
     * First check if the translation fault can be resolved by the P2M subsystem.
     * If that's the case nothing else to do.
     */
    if (p2m_resolve_translation_fault(current->domain, gaddr_to_gfn(gpa))) return true;

    if (is_data && try_map_mmio(gaddr_to_gfn(gpa))) return true;

    return false;
}

static void do_trap_stage2_abort_guest(struct cpu_user_regs *regs, const union hsr hsr) {
    /*
     * The encoding of hsr_iabt is a subset of hsr_dabt. So use
     * hsr_dabt to represent an abort fault.
     */
    const struct hsr_xabt xabt = hsr.xabt;
    int rc;
    vaddr_t gva;
    paddr_t gpa;
    uint8_t fsc = xabt.fsc & ~FSC_LL_MASK;
    bool is_data = (hsr.ec == HSR_EC_DATA_ABORT_LOWER_EL);
    mmio_info_t info;
    enum io_state state;

    /*
     * If this bit has been set, it means that this stage-2 abort is caused
     * by a guest external abort. We treat this stage-2 abort as guest SError.
     */
    if (xabt.eat) return __do_trap_serror(regs, true);

    gva = get_hfar(is_data);

    if (hpfar_is_valid(xabt.s1ptw, fsc))
        gpa = get_faulting_ipa(gva);
    else {
        /*
         * Flush the TLB to make sure the DTLB is clear before
         * doing GVA->IPA translation. If we got here because of
         * an entry only present in the ITLB, this translation may
         * still be inaccurate.
         */
        if (!is_data) flush_guest_tlb_local();

        rc = gva_to_ipa(gva, &gpa, GV2M_READ);
        /*
         * We may not be able to translate because someone is
         * playing with the Stage-2 page table of the domain.
         * Return to the guest.
         */
        if (rc == -EFAULT) return; /* Try again */
    }

    /*
     * PRTOS: For idle domain (PRTOS partitions), try MMIO emulation first
     * (needed for VGIC GICD/GICR access from Linux guests), then fall back
     * to Health Monitor for genuine faults.
     */
    if (is_idle_domain(current->domain)) {
        /* Try VGIC MMIO emulation for data aborts with valid syndrome */
        if (is_data && hsr.dabt.valid) {
            extern int prtos_mmio_dispatch(struct cpu_user_regs *regs,
                                           paddr_t gpa,
                                           int is_write, int reg, int size);
            if (prtos_mmio_dispatch(regs, gpa,
                                    hsr.dabt.write, hsr.dabt.reg,
                                    1 << hsr.dabt.size) == 0) {
                advance_pc(regs, hsr);
                return;
            }
        }
        /* Not MMIO or unhandled — route to PRTOS Health Monitor */
        extern void prtos_stage2_fault_dispatch(uint64_t pc, uint64_t cpsr, int is_data);
        prtos_stage2_fault_dispatch(regs->pc, regs->cpsr, is_data);
        return;
    }

    switch (fsc) {
        case FSC_FLT_PERM: {
            const struct npfec npfec = {.insn_fetch = !is_data,
                                        .read_access = is_data && !hsr.dabt.write,
                                        .write_access = is_data && hsr.dabt.write,
                                        .gla_valid = 1,
                                        .kind = xabt.s1ptw ? npfec_kind_in_gpt : npfec_kind_with_gla};

            p2m_mem_access_check(gpa, gva, npfec);
            /*
             * The only way to get here right now is because of mem_access,
             * thus reinjecting the exception to the guest is never required.
             */
            return;
        }
        case FSC_FLT_TRANS: {
            info.gpa = gpa;
            info.dabt = hsr.dabt;

            /*
             * Assumption :- Most of the times when we get a data abort and the ISS
             * is invalid or an instruction abort, the underlying cause is that the
             * page tables have not been set up correctly.
             */
            if (!is_data || !info.dabt.valid) {
                if (check_p2m(is_data, gpa)) return;

                /*
                 * If the instruction abort could not be resolved by setting the
                 * appropriate bits in the translation table, then PRTOS should
                 * forward the abort to the guest.
                 */
                if (!is_data) goto inject_abt;
            }

            try_decode_instruction(regs, &info);

            /*
             * If PRTOS could not decode the instruction or encountered an error
             * while decoding, then it should forward the abort to the guest.
             */
            if (info.dabt_instr.state == INSTR_ERROR) goto inject_abt;

            state = try_handle_mmio(regs, &info);

            switch (state) {
                case IO_ABORT:
                    goto inject_abt;
                case IO_HANDLED:
                    /*
                     * If the instruction was decoded and has executed successfully
                     * on the MMIO region, then PRTOS should execute the next part of
                     * the instruction. (for eg increment the rn if it is a
                     * post-indexing instruction.
                     */
                    finalize_instr_emulation(&info.dabt_instr);
                    advance_pc(regs, hsr);
                    return;
                case IO_RETRY:
                    /* finish later */
                    return;
                case IO_UNHANDLED:
                    /* IO unhandled, try another way to handle it. */
                    break;
            }

            /*
             * If the instruction syndrome was invalid, then we already checked if
             * this was due to a P2M fault. So no point to check again as the result
             * will be the same.
             */
            if ((info.dabt_instr.state == INSTR_VALID) && check_p2m(is_data, gpa)) return;

            break;
        }
        default:
            gprintk(PRTOSLOG_WARNING, "Unsupported FSC: HSR=%#" PRIregister " DFSC=%#x\n", hsr.bits, xabt.fsc);
            break;
    }

inject_abt:
    gdprintk(PRTOSLOG_DEBUG, "HSR=%#" PRIregister " pc=%#" PRIregister " gva=%#" PRIvaddr " gpa=%#" PRIpaddr "\n", hsr.bits, regs->pc, gva, gpa);
    if (is_data)
        inject_dabt_exception(regs, gva, hsr.len);
    else
        inject_iabt_exception(regs, gva, hsr.len);
}

static inline bool needs_ssbd_flip(struct vcpu *v) {
    if (!check_workaround_ssbd()) return false;

    return !(v->arch.cpu_info->flags & CPUINFO_WORKAROUND_2_FLAG) && cpu_require_ssbd_mitigation();
}

/*
 * Actions that needs to be done after entering the hypervisor from the
 * guest and before the interrupts are unmasked.
 */
void asmlinkage enter_hypervisor_from_guest_preirq(void) {
    struct vcpu *v = current;

    /* If the guest has disabled the workaround, bring it back on. */
    if (needs_ssbd_flip(v)) arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_2_FID, 1, NULL);
}

/*
 * Actions that needs to be done after entering the hypervisor from the
 * guest and before we handle any request. Depending on the exception trap,
 * this may be called with interrupts unmasked.
 */
void asmlinkage enter_hypervisor_from_guest(void) {
    struct vcpu *v = current;

    /*
     * If we pended a virtual abort, preserve it until it gets cleared.
     * See ARM ARM DDI 0487A.j D1.14.3 (Virtual Interrupts) for details,
     * but the crucial bit is "On taking a vSError interrupt, HCR_EL2.VSE
     * (alias of HCR.VA) is cleared to 0."
     */
    if (v->arch.hcr_el2 & HCR_VA) v->arch.hcr_el2 = READ_SYSREG(HCR_EL2);

#ifdef CONFIG_NEW_VGIC
    /*
     * We need to update the state of our emulated devices using level
     * triggered interrupts before syncing back the VGIC state.
     *
     * TODO: Investigate whether this is necessary to do on every
     * trap and how it can be optimised.
     */
    vtimer_update_irqs(v);
    vcpu_update_evtchn_irq(v);
#endif

    vgic_sync_from_lrs(v);
}

void asmlinkage do_trap_guest_sync(struct cpu_user_regs *regs) {
    const union hsr hsr = {.bits = regs->hsr};

    switch (hsr.ec) {
        case HSR_EC_WFI_WFE:
            /*
             * HCR_EL2.TWI, HCR_EL2.TWE
             *
             * ARMv7 (DDI 0406C.b): B1.14.9
             * ARMv8 (DDI 0487A.d): D1-1505 Table D1-51
             */
            if (!check_conditional_instr(regs, hsr)) {
                advance_pc(regs, hsr);
                return;
            }
            if (hsr.wfi_wfe.ti) {
                /* Yield the VCPU for WFE */
                perfc_incr(trap_wfe);
                vcpu_yield();
            } else {
                /* Block the VCPU for WFI */
                perfc_incr(trap_wfi);
                vcpu_block_unless_event_pending(current);
            }
            advance_pc(regs, hsr);
            break;
        case HSR_EC_CP15_32:
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_cp15_32);
            do_cp15_32(regs, hsr);
            break;
        case HSR_EC_CP15_64:
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_cp15_64);
            do_cp15_64(regs, hsr);
            break;
        case HSR_EC_CP14_32:
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_cp14_32);
            do_cp14_32(regs, hsr);
            break;
        case HSR_EC_CP14_64:
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_cp14_64);
            do_cp14_64(regs, hsr);
            break;
        case HSR_EC_CP14_DBG:
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_cp14_dbg);
            do_cp14_dbg(regs, hsr);
            break;
        case HSR_EC_CP10:
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_cp10);
            do_cp10(regs, hsr);
            break;
        case HSR_EC_CP:
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_cp);
            do_cp(regs, hsr);
            break;
        case HSR_EC_SMC32:
            /*
             * HCR_EL2.TSC
             *
             * ARMv7 (DDI 0406C.b): B1.14.8
             * ARMv8 (DDI 0487A.d): D1-1501 Table D1-44
             */
            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_smc32);
            do_trap_smc(regs, hsr);
            break;
        case HSR_EC_HVC32: {
            register_t nr;

            GUEST_BUG_ON(!regs_mode_is_32bit(regs));
            perfc_incr(trap_hvc32);
#ifndef NDEBUG
            if ((hsr.iss & 0xff00) == 0xff00) return do_debug_trap(regs, hsr.iss & 0x00ff);
#endif
            if (hsr.iss == 0) return do_trap_hvc_smccc(regs);
            nr = regs->r12;
            do_trap_hypercall(regs, &nr, hsr);
            regs->r12 = (uint32_t)nr;
            break;
        }
#ifdef CONFIG_ARM_64
        case HSR_EC_HVC64:
            GUEST_BUG_ON(regs_mode_is_32bit(regs));
            perfc_incr(trap_hvc64);
#ifndef NDEBUG
            if ((hsr.iss & 0xff00) == 0xff00) return do_debug_trap(regs, hsr.iss & 0x00ff);
#endif
            if (hsr.iss == 0) {
                /* PRTOS AArch64 hypercall: hvc #0, x0 = hypercall number */
                extern int prtos_do_hvc(struct cpu_user_regs * regs);
                if (prtos_do_hvc(regs)) break;
                return do_trap_hvc_smccc(regs);
            }
            do_trap_hypercall(regs, &regs->x16, hsr);
            break;
        case HSR_EC_SMC64:
            /*
             * HCR_EL2.TSC
             *
             * ARMv8 (DDI 0487A.d): D1-1501 Table D1-44
             */
            GUEST_BUG_ON(regs_mode_is_32bit(regs));
            perfc_incr(trap_smc64);
            /* PRTOS: handle PSCI SMC calls for idle-domain (PRTOS partitions) */
            if (is_idle_domain(current->domain)) {
                extern int prtos_psci_handle(struct cpu_user_regs *regs);
                if (prtos_psci_handle(regs)) {
                    advance_pc(regs, hsr);  /* SMC: must advance PC past the SMC insn */
                    break;
                }
            }
            do_trap_smc(regs, hsr);
            break;
        case HSR_EC_SYSREG:
            GUEST_BUG_ON(regs_mode_is_32bit(regs));
            perfc_incr(trap_sysreg);
            /*
             * PRTOS: intercept sysreg traps for idle-domain (hw-virt partitions)
             * before reaching PRTOS's do_sysreg/vgic_emulate which dereference
             * domain->arch.vgic (NULL for idle domain).
             */
            if (is_idle_domain(current->domain)) {
                extern int prtos_sysreg_dispatch(struct cpu_user_regs *regs,
                                                  uint64_t hsr_bits);
                if (prtos_sysreg_dispatch(regs, hsr.bits)) {
                    advance_pc(regs, hsr);
                    break;
                }
            }
            do_sysreg(regs, hsr);
            break;
        case HSR_EC_SVE:
            GUEST_BUG_ON(regs_mode_is_32bit(regs));
            gprintk(PRTOSLOG_WARNING, "Domain tried to use SVE while not allowed\n");
            inject_undef_exception(regs, hsr);
            break;
#endif

        case HSR_EC_INSTR_ABORT_LOWER_EL:
            perfc_incr(trap_iabt);
            do_trap_stage2_abort_guest(regs, hsr);
            break;
        case HSR_EC_DATA_ABORT_LOWER_EL:
            perfc_incr(trap_dabt);
            do_trap_stage2_abort_guest(regs, hsr);
            break;

        default:
            gprintk(PRTOSLOG_WARNING, "Unknown Guest Trap. HSR=%#" PRIregister " EC=0x%x IL=%x Syndrome=0x%" PRIx32 "\n", hsr.bits, hsr.ec, hsr.len, hsr.iss);
            inject_undef_exception(regs, hsr);
            break;
    }
}

void do_trap_hyp_sync(struct cpu_user_regs *regs) {
    const union hsr hsr = {.bits = regs->hsr};

    switch (hsr.ec) {
#ifdef CONFIG_ARM_64
        case HSR_EC_BRK:
            do_trap_brk(regs, hsr);
            break;
        case HSR_EC_SVE:
            /* An SVE exception is a bug somewhere in hypervisor code */
            do_unexpected_trap("SVE trap at EL2", regs);
            ASSERT_UNREACHABLE();
            break;
#endif
        case HSR_EC_DATA_ABORT_CURR_EL:
        case HSR_EC_INSTR_ABORT_CURR_EL: {
            bool is_data = (hsr.ec == HSR_EC_DATA_ABORT_CURR_EL);
            const char *fault = (is_data) ? "Data Abort" : "Instruction Abort";

            printk("%s Trap. Syndrome=%#x\n", fault, hsr.iss);
            /*
             * FAR may not be valid for a Synchronous External abort other
             * than translation table walk.
             */
            if (hsr.xabt.fsc == FSC_SEA && hsr.xabt.fnv)
                printk("Invalid FAR, not walking the hypervisor tables\n");
            else
                dump_hyp_walk(get_hfar(is_data));

            do_unexpected_trap(fault, regs);
            ASSERT_UNREACHABLE();
            break;
        }
        default:
            printk("Hypervisor Trap. HSR=%#" PRIregister " EC=0x%x IL=%x Syndrome=0x%" PRIx32 "\n", hsr.bits, hsr.ec, hsr.len, hsr.iss);
            do_unexpected_trap("Hypervisor", regs);
    }
}

void do_trap_hyp_serror(struct cpu_user_regs *regs) {
    __do_trap_serror(regs, VABORT_GEN_BY_GUEST(regs));
}

void do_trap_guest_serror(struct cpu_user_regs *regs) {
    __do_trap_serror(regs, true);
}

void asmlinkage do_trap_irq(struct cpu_user_regs *regs) {
#if CONFIG_STATIC_IRQ_ROUTING
    static_gic_interrupt(regs);
#else
    gic_interrupt(regs, 0);
#endif
}


static void check_for_pcpu_work(void) {
    ASSERT(!local_irq_is_enabled());

    while (softirq_pending(smp_processor_id())) {
        local_irq_enable();
        do_softirq();
        local_irq_disable();
    }
}

/*
 * Process pending work for the vCPU. Any call should be fast or
 * implement preemption.
 */
static bool check_for_vcpu_work(void) {
    struct vcpu *v = current;

    if (has_vpci(v->domain)) {
        bool pending;

        local_irq_enable();
        pending = vpci_process_pending(v);
        local_irq_disable();

        if (pending) return true;
    }

#ifdef CONFIG_IOREQ_SERVER
    if (domain_has_ioreq_server(v->domain)) {
        bool handled;

        local_irq_enable();
        handled = vcpu_ioreq_handle_completion(v);
        local_irq_disable();

        if (!handled) return true;
    }
#endif

    if (likely(!v->arch.need_flush_to_ram)) return false;

    /*
     * Give a chance for the pCPU to process work before handling the vCPU
     * pending work.
     */
    check_for_pcpu_work();

    local_irq_enable();
    p2m_flush_vm(v);
    local_irq_disable();

    return false;
}

/*
 * Actions that needs to be done before entering the guest. This is the
 * last thing executed before the guest context is fully restored.
 *
 * The function will return with IRQ masked.
 */
/*
 * Per-CPU pointer to the current guest's saved cpu_user_regs on the
 * kthread stack.  Set by leave_hypervisor_to_guest() (called from
 * entry.S with x0 = sp, which points to the saved frame) so that
 * PRTOS's fix_stack() can find the guest regs after a CONTEXT_SWITCH
 * without relying on PRTOS's STACK_SIZE-aligned per-CPU stack layout.
 */
struct cpu_user_regs *prtos_current_guest_regs_percpu[CONFIG_NO_CPUS];

void asmlinkage leave_hypervisor_to_guest(struct cpu_user_regs *regs) {
    prtos_current_guest_regs_percpu[smp_processor_id()] = regs;

    local_irq_disable();

    /*
     * check_for_vcpu_work() may return true if there are more work to before
     * the vCPU can safely resume. This gives us an opportunity to deschedule
     * the vCPU if needed.
     */
    while (check_for_vcpu_work()) check_for_pcpu_work();
    check_for_pcpu_work();


    /* PRTOS: deliver pending virtual IRQs to partition before ERET */
    {
        extern void prtos_raise_pend_irqs_aarch64(void);
        prtos_raise_pend_irqs_aarch64();
    }

    /* PRTOS: flush pending hw-virt IRQs to ICH_LR registers (for Linux guests) */
    {
        extern void prtos_vgic_flush_lrs_current(void);
        prtos_vgic_flush_lrs_current();
    }

    vgic_sync_to_lrs();

    /*
     * If the SErrors handle option is "DIVERSE", we have to prevent
     * slipping the hypervisor SError to guest. In this option, before
     * returning from trap, we have to synchronize SErrors to guarantee
     * that the pending SError would be caught in hypervisor.
     *
     * If option is NOT "DIVERSE", SKIP_SYNCHRONIZE_SERROR_ENTRY_EXIT
     * will be set to cpu_hwcaps. This means we can use the alternative
     * to skip synchronizing SErrors for other SErrors handle options.
     */
    SYNCHRONIZE_SERROR(SKIP_SYNCHRONIZE_SERROR_ENTRY_EXIT);

    /*
     * The hypervisor runs with the workaround always present.
     * If the guest wants it disabled, so be it...
     */
    if (needs_ssbd_flip(current)) arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_2_FID, 0, NULL);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_traps.c === */
/* === BEGIN INLINED: alternative.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * alternative runtime patching
 * inspired by the x86 version
 *
 * Copyright (C) 2014-2016 ARM Ltd.
 */

#include <prtos_init.h>
#include <prtos_types.h>
#include <prtos_kernel.h>
#include <prtos_mm.h>
#include <prtos_vmap.h>
#include <prtos_smp.h>
#include <prtos_stop_machine.h>
#include <prtos_virtual_region.h>
#include <asm_alternative.h>
#include <asm_atomic.h>
#include <asm_byteorder.h>
#include <asm_cpufeature.h>
#include <asm_insn.h>
#include <asm_page.h>

/* Override macros from asm/page.h to make them work with mfn_t */
#undef virt_to_mfn
#define virt_to_mfn(va) _mfn(__virt_to_mfn(va))

extern const struct alt_instr __alt_instructions[], __alt_instructions_end[];

struct alt_region {
    const struct alt_instr *begin;
    const struct alt_instr *end;
};

/*
 * Check if the target PC is within an alternative block.
 */
static bool branch_insn_requires_update(const struct alt_instr *alt,
                                        unsigned long pc)
{
    unsigned long replptr;

    if ( is_active_kernel_text(pc) )
        return true;

    replptr = (unsigned long)ALT_REPL_PTR(alt);
    if ( pc >= replptr && pc <= (replptr + alt->repl_len) )
        return false;

    /*
     * Branching into *another* alternate sequence is doomed, and
     * we're not even trying to fix it up.
     */
    BUG();
}

static u32 get_alt_insn(const struct alt_instr *alt,
                        const u32 *insnptr, const u32 *altinsnptr)
{
    u32 insn;

    insn = le32_to_cpu(*altinsnptr);

    if ( insn_is_branch_imm(insn) )
    {
        s32 offset = insn_get_branch_offset(insn);
        unsigned long target;

        target = (unsigned long)altinsnptr + offset;

        /*
         * If we're branching inside the alternate sequence,
         * do not rewrite the instruction, as it is already
         * correct. Otherwise, generate the new instruction.
         */
        if ( branch_insn_requires_update(alt, target) )
        {
            offset = target - (unsigned long)insnptr;
            insn = insn_set_branch_offset(insn, offset);
        }
    }

    return insn;
}

static void patch_alternative(const struct alt_instr *alt,
                              const uint32_t *origptr,
                              uint32_t *updptr, int nr_inst)
{
    const uint32_t *replptr;
    unsigned int i;

    replptr = ALT_REPL_PTR(alt);
    for ( i = 0; i < nr_inst; i++ )
    {
        uint32_t insn;

        insn = get_alt_insn(alt, origptr + i, replptr + i);
        updptr[i] = cpu_to_le32(insn);
    }
}

/*
 * The region patched should be read-write to allow __apply_alternatives
 * to replacing the instructions when necessary.
 *
 * @update_offset: Offset between the region patched and the writable
 * region for the update. 0 if the patched region is writable.
 */
static int __apply_alternatives(const struct alt_region *region,
                                paddr_t update_offset)
{
    const struct alt_instr *alt;
    const u32 *origptr;
    u32 *updptr;
    alternative_cb_t alt_cb;

    printk(PRTOSLOG_INFO "alternatives: Patching with alt table %p -> %p\n",
           region->begin, region->end);

    for ( alt = region->begin; alt < region->end; alt++ )
    {
        int nr_inst;

        /* Use ARM_CB_PATCH as an unconditional patch */
        if ( alt->cpufeature < ARM_CB_PATCH &&
             !cpus_have_cap(alt->cpufeature) )
            continue;

        if ( alt->cpufeature == ARM_CB_PATCH )
            BUG_ON(alt->repl_len != 0);
        else
            BUG_ON(alt->repl_len != alt->orig_len);

        origptr = ALT_ORIG_PTR(alt);
        updptr = (void *)origptr + update_offset;

        nr_inst = alt->orig_len / ARCH_PATCH_INSN_SIZE;

        if ( alt->cpufeature < ARM_CB_PATCH )
            alt_cb = patch_alternative;
        else
            alt_cb = ALT_REPL_PTR(alt);

        alt_cb(alt, origptr, updptr, nr_inst);

        /* Ensure the new instructions reached the memory and nuke */
        clean_and_invalidate_dcache_va_range(origptr,
                                             (sizeof (*origptr) * nr_inst));
    }

    /* Nuke the instruction cache */
    invalidate_icache();

    return 0;
}

/*
 * This function should only be called during boot and before CPU0 jump
 * into the idle_loop.
 */
void __init apply_alternatives_all(void)
{
}

#ifdef CONFIG_LIVEPATCH
int apply_alternatives(const struct alt_instr *start, const struct alt_instr *end)
{
    const struct alt_region region = {
        .begin = start,
        .end = end,
    };

    return __apply_alternatives(&region, 0);
}
#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: alternative.c === */
/* === BEGIN INLINED: cpuerrata.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <prtos_cpu.h>
#include <prtos_cpumask.h>
#include <prtos_init.h>
#include <prtos_mm.h>
#include <prtos_param.h>
#include <prtos_sizes.h>
#include <prtos_smp.h>
#include <prtos_spinlock.h>
#include <prtos_vmap.h>
#include <prtos_warning.h>
#include <prtos_notifier.h>
#include <asm_cpufeature.h>
#include <asm_cpuerrata.h>
#include <asm_insn.h>
#include <asm_psci.h>

/* Override macros from asm/page.h to make them work with mfn_t */
#undef virt_to_mfn
#define virt_to_mfn(va) _mfn(__virt_to_mfn(va))

/* Hardening Branch predictor code for Arm64 */
#ifdef CONFIG_ARM64_HARDEN_BRANCH_PREDICTOR

#define VECTOR_TABLE_SIZE SZ_2K

/*
 * Number of available table vectors (this should be in-sync with
 * arch/arm64/bpi.S
 */
#define NR_BPI_HYP_VECS 4

extern char __bp_harden_hyp_vecs_start[], __bp_harden_hyp_vecs_end[];

/*
 * Key for each slot. This is used to find whether a specific workaround
 * had a slot assigned.
 *
 * The key is virtual address of the vector workaround
 */
static uintptr_t bp_harden_slot_key[NR_BPI_HYP_VECS];

/*
 * [hyp_vec_start, hyp_vec_end[ corresponds to the first 31 instructions
 * of each vector. The last (i.e 32th) instruction is used to branch to
 * the original entry.
 *
 * Those instructions will be copied on each vector to harden them.
 */
static bool copy_hyp_vect_bpi(unsigned int slot, const char *hyp_vec_start,
                              const char *hyp_vec_end)
{
    void *dst_remapped;
    const void *dst = __bp_harden_hyp_vecs_start + slot * VECTOR_TABLE_SIZE;
    unsigned int i;
    mfn_t dst_mfn = virt_to_mfn(dst);

    BUG_ON(((hyp_vec_end - hyp_vec_start) / 4) > 31);

    /*
     * Vectors are part of the text that are mapped read-only. So re-map
     * the vector table to be able to update vectors.
     */
    dst_remapped = __vmap(&dst_mfn,
                          1UL << get_order_from_bytes(VECTOR_TABLE_SIZE),
                          1, 1, PAGE_HYPERVISOR, VMAP_DEFAULT);
    if ( !dst_remapped )
        return false;

    dst_remapped += (vaddr_t)dst & ~PAGE_MASK;

    for ( i = 0; i < VECTOR_TABLE_SIZE; i += 0x80 )
    {
        memcpy(dst_remapped + i, hyp_vec_start, hyp_vec_end - hyp_vec_start);
    }

    clean_dcache_va_range(dst_remapped, VECTOR_TABLE_SIZE);
    invalidate_icache();

    vunmap((void *)((vaddr_t)dst_remapped & PAGE_MASK));

    return true;
}

static bool __maybe_unused
install_bp_hardening_vec(const struct arm_cpu_capabilities *entry,
                         const char *hyp_vec_start,
                         const char *hyp_vec_end,
                         const char *desc)
{
    static int last_slot = -1;
    static DEFINE_SPINLOCK(bp_lock);
    unsigned int i, slot = -1;
    bool ret = true;

    /*
     * Enable callbacks are called on every CPU based on the
     * capabilities. So double-check whether the CPU matches the
     * entry.
     */
    if ( !entry->matches(entry) )
        return true;

    printk(PRTOSLOG_INFO "CPU%u will %s on exception entry\n",
           smp_processor_id(), desc);

    spin_lock(&bp_lock);

    /*
     * Look up whether the hardening vector had a slot already
     * assigned.
     */
    for ( i = 0; i < 4; i++ )
    {
        if ( bp_harden_slot_key[i] == (uintptr_t)hyp_vec_start )
        {
            slot = i;
            break;
        }
    }

    if ( slot == -1 )
    {
        last_slot++;
        /* Check we don't overrun the number of slots available. */
        BUG_ON(NR_BPI_HYP_VECS <= last_slot);

        slot = last_slot;
        ret = copy_hyp_vect_bpi(slot, hyp_vec_start, hyp_vec_end);

        /* Only update the slot if the copy succeeded. */
        if ( ret )
            bp_harden_slot_key[slot] = (uintptr_t)hyp_vec_start;
    }

    if ( ret )
    {
        /* Install the new vector table. */
        WRITE_SYSREG((vaddr_t)(__bp_harden_hyp_vecs_start + slot * VECTOR_TABLE_SIZE),
                     VBAR_EL2);
        isb();
    }

    spin_unlock(&bp_lock);

    return ret;
}

extern char __smccc_workaround_smc_start_1[], __smccc_workaround_smc_end_1[];
extern char __smccc_workaround_smc_start_3[], __smccc_workaround_smc_end_3[];
extern char __mitigate_spectre_bhb_clear_insn_start[],
            __mitigate_spectre_bhb_clear_insn_end[];
extern char __mitigate_spectre_bhb_loop_start_8[],
            __mitigate_spectre_bhb_loop_end_8[];
extern char __mitigate_spectre_bhb_loop_start_24[],
            __mitigate_spectre_bhb_loop_end_24[];
extern char __mitigate_spectre_bhb_loop_start_32[],
            __mitigate_spectre_bhb_loop_end_32[];

static int enable_smccc_arch_workaround_1(void *data)
{
    struct arm_smccc_res res;
    const struct arm_cpu_capabilities *entry = data;

    /*
     * Enable callbacks are called on every CPU based on the
     * capabilities. So double-check whether the CPU matches the
     * entry.
     */
    if ( !entry->matches(entry) )
        return 0;

    /*
     * No need to install hardened vector when the processor has
     * ID_AA64PRF0_EL1.CSV2 set.
     */
    if ( cpu_data[smp_processor_id()].pfr64.csv2 )
        return 0;

    if ( smccc_ver < SMCCC_VERSION(1, 1) )
        goto warn;

    arm_smccc_1_1_smc(ARM_SMCCC_ARCH_FEATURES_FID,
                      ARM_SMCCC_ARCH_WORKAROUND_1_FID, &res);
    /* The return value is in the lower 32-bits. */
    if ( (int)res.a0 < 0 )
        goto warn;

    return !install_bp_hardening_vec(entry,__smccc_workaround_smc_start_1,
                                     __smccc_workaround_smc_end_1,
                                     "call ARM_SMCCC_ARCH_WORKAROUND_1");

warn:
    printk_once("**** No support for ARM_SMCCC_ARCH_WORKAROUND_1. ****\n"
                "**** Please update your firmware.                ****\n");

    return 0;
}

/*
 * Spectre BHB Mitigation
 *
 * CPU is either:
 * - Having CVS2.3 so it is not affected.
 * - Having ECBHB and is clearing the branch history buffer when an exception
 *   to a different exception level is happening so no mitigation is needed.
 * - Mitigating using a loop on exception entry (number of loop depending on
 *   the CPU).
 * - Mitigating using the firmware.
 */
static int enable_spectre_bhb_workaround(void *data)
{
    const struct arm_cpu_capabilities *entry = data;

    /*
     * Enable callbacks are called on every CPU based on the capabilities, so
     * double-check whether the CPU matches the entry.
     */
    if ( !entry->matches(entry) )
        return 0;

    if ( cpu_data[smp_processor_id()].pfr64.csv2 == 3 )
        return 0;

    if ( cpu_data[smp_processor_id()].mm64.ecbhb )
        return 0;

    if ( cpu_data[smp_processor_id()].isa64.clearbhb )
        return !install_bp_hardening_vec(entry,
                                    __mitigate_spectre_bhb_clear_insn_start,
                                    __mitigate_spectre_bhb_clear_insn_end,
                                     "use clearBHB instruction");

    /* Apply solution depending on hwcaps set on arm_errata */
    if ( cpus_have_cap(ARM_WORKAROUND_BHB_LOOP_8) )
        return !install_bp_hardening_vec(entry,
                                         __mitigate_spectre_bhb_loop_start_8,
                                         __mitigate_spectre_bhb_loop_end_8,
                                         "use 8 loops workaround");

    if ( cpus_have_cap(ARM_WORKAROUND_BHB_LOOP_24) )
        return !install_bp_hardening_vec(entry,
                                         __mitigate_spectre_bhb_loop_start_24,
                                         __mitigate_spectre_bhb_loop_end_24,
                                         "use 24 loops workaround");

    if ( cpus_have_cap(ARM_WORKAROUND_BHB_LOOP_32) )
        return !install_bp_hardening_vec(entry,
                                         __mitigate_spectre_bhb_loop_start_32,
                                         __mitigate_spectre_bhb_loop_end_32,
                                         "use 32 loops workaround");

    if ( cpus_have_cap(ARM_WORKAROUND_BHB_SMCC_3) )
    {
        struct arm_smccc_res res;

        if ( smccc_ver < SMCCC_VERSION(1, 1) )
            goto warn;

        arm_smccc_1_1_smc(ARM_SMCCC_ARCH_FEATURES_FID,
                          ARM_SMCCC_ARCH_WORKAROUND_3_FID, &res);
        /* The return value is in the lower 32-bits. */
        if ( (int)res.a0 < 0 )
        {
            /*
             * On processor affected with CSV2=0, workaround 1 will mitigate
             * both Spectre v2 and BHB so use it when available
             */
            if ( enable_smccc_arch_workaround_1(data) )
                return 1;

            goto warn;
        }

        return !install_bp_hardening_vec(entry,__smccc_workaround_smc_start_3,
                                         __smccc_workaround_smc_end_3,
                                         "call ARM_SMCCC_ARCH_WORKAROUND_3");
    }

warn:
    printk_once("**** No support for any spectre BHB workaround.  ****\n"
                "**** Please update your firmware.                ****\n");

    return 0;
}

#endif /* CONFIG_ARM64_HARDEN_BRANCH_PREDICTOR */

/* Hardening Branch predictor code for Arm32 */
#ifdef CONFIG_ARM32_HARDEN_BRANCH_PREDICTOR

/*
 * Per-CPU vector tables to use when returning to the guests. They will
 * only be used on platform requiring to harden the branch predictor.
 */
DEFINE_PER_CPU_READ_MOSTLY(const char *, bp_harden_vecs);

extern char hyp_traps_vector_bp_inv[];
extern char hyp_traps_vector_ic_inv[];

static void __maybe_unused
install_bp_hardening_vecs(const struct arm_cpu_capabilities *entry,
                          const char *hyp_vecs, const char *desc)
{
    /*
     * Enable callbacks are called on every CPU based on the
     * capabilities. So double-check whether the CPU matches the
     * entry.
     */
    if ( !entry->matches(entry) )
        return;

    printk(PRTOSLOG_INFO "CPU%u will %s on guest exit\n",
           smp_processor_id(), desc);
    this_cpu(bp_harden_vecs) = hyp_vecs;
}

static int enable_bp_inv_hardening(void *data)
{
    install_bp_hardening_vecs(data, hyp_traps_vector_bp_inv,
                              "execute BPIALL");
    return 0;
}

static int enable_ic_inv_hardening(void *data)
{
    install_bp_hardening_vecs(data, hyp_traps_vector_ic_inv,
                              "execute ICIALLU");
    return 0;
}

#endif

#ifdef CONFIG_ARM_SSBD

enum ssbd_state ssbd_state = ARM_SSBD_RUNTIME;

static int __init parse_spec_ctrl(const char *s)
{
    const char *ss;
    int rc = 0;

    do {
        ss = strchr(s, ',');
        if ( !ss )
            ss = strchr(s, '\0');

        if ( !strncmp(s, "ssbd=", 5) )
        {
            s += 5;

            if ( !cmdline_strcmp(s, "force-disable") )
                ssbd_state = ARM_SSBD_FORCE_DISABLE;
            else if ( !cmdline_strcmp(s, "runtime") )
                ssbd_state = ARM_SSBD_RUNTIME;
            else if ( !cmdline_strcmp(s, "force-enable") )
                ssbd_state = ARM_SSBD_FORCE_ENABLE;
            else
                rc = -EINVAL;
        }
        else
            rc = -EINVAL;

        s = ss + 1;
    } while ( *ss );

    return rc;
}
custom_param("spec-ctrl", parse_spec_ctrl);

/* Arm64 only for now as for Arm32 the workaround is currently handled in C. */
#ifdef CONFIG_ARM_64
void asmlinkage __init arm_enable_wa2_handling(const struct alt_instr *alt,
                                               const uint32_t *origptr,
                                               uint32_t *updptr, int nr_inst)
{
    BUG_ON(nr_inst != 1);

    /*
     * Only allow mitigation on guest ARCH_WORKAROUND_2 if the SSBD
     * state allow it to be flipped.
     */
    if ( get_ssbd_state() == ARM_SSBD_RUNTIME )
        *updptr = aarch64_insn_gen_nop();
}
#endif

/*
 * Assembly code may use the variable directly, so we need to make sure
 * it fits in a register.
 */
DEFINE_PER_CPU_READ_MOSTLY(register_t, ssbd_callback_required);

static bool has_ssbd_mitigation(const struct arm_cpu_capabilities *entry)
{
    struct arm_smccc_res res;
    bool required;

    if ( smccc_ver < SMCCC_VERSION(1, 1) )
        return false;

    arm_smccc_1_1_smc(ARM_SMCCC_ARCH_FEATURES_FID,
                      ARM_SMCCC_ARCH_WORKAROUND_2_FID, &res);

    switch ( (int)res.a0 )
    {
    case ARM_SMCCC_NOT_SUPPORTED:
        ssbd_state = ARM_SSBD_UNKNOWN;
        return false;

    case ARM_SMCCC_NOT_REQUIRED:
        ssbd_state = ARM_SSBD_MITIGATED;
        return false;

    case ARM_SMCCC_SUCCESS:
        required = true;
        break;

    case 1: /* Mitigation not required on this CPU. */
        required = false;
        break;

    default:
        ASSERT_UNREACHABLE();
        return false;
    }

    switch ( ssbd_state )
    {
    case ARM_SSBD_FORCE_DISABLE:
        printk_once("%s disabled from command-line\n", entry->desc);

        arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_2_FID, 0, NULL);
        required = false;
        break;

    case ARM_SSBD_RUNTIME:
        if ( required )
        {
            this_cpu(ssbd_callback_required) = 1;
            arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_2_FID, 1, NULL);
        }

        break;

    case ARM_SSBD_FORCE_ENABLE:
        printk_once("%s forced from command-line\n", entry->desc);

        arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_2_FID, 1, NULL);
        required = true;
        break;

    default:
        ASSERT_UNREACHABLE();
        return false;
    }

    return required;
}
#endif

#define MIDR_RANGE(model, min, max)     \
    .matches = is_affected_midr_range,  \
    .midr_model = (model),              \
    .midr_range_min = (min),            \
    .midr_range_max = (max)

#define MIDR_ALL_VERSIONS(model)        \
    .matches = is_affected_midr_range,  \
    .midr_model = (model),              \
    .midr_range_min = 0,                \
    .midr_range_max = (MIDR_VARIANT_MASK | MIDR_REVISION_MASK)

static bool __maybe_unused
is_affected_midr_range(const struct arm_cpu_capabilities *entry)
{
    return MIDR_IS_CPU_MODEL_RANGE(current_cpu_data.midr.bits, entry->midr_model,
                                   entry->midr_range_min,
                                   entry->midr_range_max);
}

static const struct arm_cpu_capabilities arm_errata[] = {
    {
        /* Cortex-A15 r0p4 */
        .desc = "ARM erratum 766422",
        .capability = ARM32_WORKAROUND_766422,
        MIDR_RANGE(MIDR_CORTEX_A15, 0x04, 0x04),
    },
#if defined(CONFIG_ARM64_ERRATUM_827319) || \
    defined(CONFIG_ARM64_ERRATUM_824069)
    {
        /* Cortex-A53 r0p[012] */
        .desc = "ARM errata 827319, 824069",
        .capability = ARM64_WORKAROUND_CLEAN_CACHE,
        MIDR_RANGE(MIDR_CORTEX_A53, 0x00, 0x02),
    },
#endif
#ifdef CONFIG_ARM64_ERRATUM_819472
    {
        /* Cortex-A53 r0[01] */
        .desc = "ARM erratum 819472",
        .capability = ARM64_WORKAROUND_CLEAN_CACHE,
        MIDR_RANGE(MIDR_CORTEX_A53, 0x00, 0x01),
    },
#endif
#ifdef CONFIG_ARM64_ERRATUM_832075
    {
        /* Cortex-A57 r0p0 - r1p2 */
        .desc = "ARM erratum 832075",
        .capability = ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE,
        MIDR_RANGE(MIDR_CORTEX_A57, 0x00,
                   (1 << MIDR_VARIANT_SHIFT) | 2),
    },
#endif
#ifdef CONFIG_ARM64_ERRATUM_834220
    {
        /* Cortex-A57 r0p0 - r1p2 */
        .desc = "ARM erratum 834220",
        .capability = ARM64_WORKAROUND_834220,
        MIDR_RANGE(MIDR_CORTEX_A57, 0x00,
                   (1 << MIDR_VARIANT_SHIFT) | 2),
    },
#endif
#ifdef CONFIG_ARM64_ERRATUM_1286807
    {
        /* Cortex-A76 r0p0 - r3p0 */
        .desc = "ARM erratum 1286807",
        .capability = ARM64_WORKAROUND_REPEAT_TLBI,
        MIDR_RANGE(MIDR_CORTEX_A76, 0, 3 << MIDR_VARIANT_SHIFT),
    },
    {
        /* Neoverse-N1 r0p0 - r3p0 */
        .desc = "ARM erratum 1286807",
        .capability = ARM64_WORKAROUND_REPEAT_TLBI,
        MIDR_RANGE(MIDR_NEOVERSE_N1, 0, 3 << MIDR_VARIANT_SHIFT),
    },
#endif
#ifdef CONFIG_ARM64_HARDEN_BRANCH_PREDICTOR
    {
        .capability = ARM_HARDEN_BRANCH_PREDICTOR,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A57),
        .enable = enable_smccc_arch_workaround_1,
    },
    {
        .capability = ARM_HARDEN_BRANCH_PREDICTOR,
        MIDR_RANGE(MIDR_CORTEX_A72, 0, 1 << MIDR_VARIANT_SHIFT),
        .enable = enable_smccc_arch_workaround_1,
    },
    {
        .capability = ARM_WORKAROUND_BHB_SMCC_3,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A73),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_SMCC_3,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A75),
        .enable = enable_spectre_bhb_workaround,
    },
    /* spectre BHB */
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_8,
        MIDR_RANGE(MIDR_CORTEX_A72, 1 << MIDR_VARIANT_SHIFT,
                   (MIDR_VARIANT_MASK | MIDR_REVISION_MASK)),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_24,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A76),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_24,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A77),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_32,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A78),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_32,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A78C),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_32,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_X1),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_32,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_X2),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_32,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A710),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_24,
        MIDR_ALL_VERSIONS(MIDR_NEOVERSE_N1),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_32,
        MIDR_ALL_VERSIONS(MIDR_NEOVERSE_N2),
        .enable = enable_spectre_bhb_workaround,
    },
    {
        .capability = ARM_WORKAROUND_BHB_LOOP_32,
        MIDR_ALL_VERSIONS(MIDR_NEOVERSE_V1),
        .enable = enable_spectre_bhb_workaround,
    },

#endif
#ifdef CONFIG_ARM32_HARDEN_BRANCH_PREDICTOR
    {
        .capability = ARM_HARDEN_BRANCH_PREDICTOR,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A12),
        .enable = enable_bp_inv_hardening,
    },
    {
        .capability = ARM_HARDEN_BRANCH_PREDICTOR,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A17),
        .enable = enable_bp_inv_hardening,
    },
    {
        .capability = ARM_HARDEN_BRANCH_PREDICTOR,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A15),
        .enable = enable_ic_inv_hardening,
    },
#endif
#ifdef CONFIG_ARM_SSBD
    {
        .desc = "Speculative Store Bypass Disabled",
        .capability = ARM_SSBD,
        .matches = has_ssbd_mitigation,
    },
#endif
#ifdef CONFIG_ARM_ERRATUM_858921
    {
        /* Cortex-A73 (all versions) */
        .desc = "ARM erratum 858921",
        .capability = ARM_WORKAROUND_858921,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A73),
    },
#endif
    {
        /* Neoverse r0p0 - r2p0 */
        .desc = "ARM erratum 1165522",
        .capability = ARM64_WORKAROUND_AT_SPECULATE,
        MIDR_RANGE(MIDR_NEOVERSE_N1, 0, 2 << MIDR_VARIANT_SHIFT),
    },
    {
        /* Cortex-A76 r0p0 - r2p0 */
        .desc = "ARM erratum 1165522",
        .capability = ARM64_WORKAROUND_AT_SPECULATE,
        MIDR_RANGE(MIDR_CORTEX_A76, 0, 2 << MIDR_VARIANT_SHIFT),
    },
    {
        .desc = "ARM erratum 1319537",
        .capability = ARM64_WORKAROUND_AT_SPECULATE,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A72),
    },
    {
        .desc = "ARM erratum 1319367",
        .capability = ARM64_WORKAROUND_AT_SPECULATE,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A57),
    },
#ifdef CONFIG_ARM64_ERRATUM_1508412
    {
        /* Cortex-A77 r0p0 - r1p0 */
        .desc = "ARM erratum 1508412 (hypervisor portion)",
        .capability = ARM64_WORKAROUND_1508412,
        MIDR_RANGE(MIDR_CORTEX_A77, 0, 1),
    },
#endif
    {
        /* Cortex-A55 (All versions as erratum is open in SDEN v14) */
        .desc = "ARM erratum 1530923",
        .capability = ARM64_WORKAROUND_AT_SPECULATE,
        MIDR_ALL_VERSIONS(MIDR_CORTEX_A55),
    },
    {},
};

void check_local_cpu_errata(void)
{
    update_cpu_capabilities(arm_errata, "enabled workaround for");
}

void __init enable_errata_workarounds(void)
{
    enable_cpu_capabilities(arm_errata);

#if defined(CONFIG_ARM64_ERRATUM_832075) || defined(CONFIG_ARM64_ERRATUM_1508412)
    if ( cpus_have_cap(ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE) ||
         cpus_have_cap(ARM64_WORKAROUND_1508412) )
    {
        printk_once("**** Guests without CPU erratum workarounds can deadlock the system! ****\n"
                    "**** Only trusted guests should be used.                             ****\n");

        /* Taint the machine has being insecure */
        add_taint(TAINT_MACHINE_INSECURE);
    }
#endif
}

static int cpu_errata_callback(struct notifier_block *nfb,
                               unsigned long action,
                               void *hcpu)
{
    int rc = 0;

    switch ( action )
    {
    case CPU_STARTING:
        /*
         * At CPU_STARTING phase no notifier shall return an error, because the
         * system is designed with the assumption that starting a CPU cannot
         * fail at this point. If an error happens here it will cause PRTOS to hit
         * the BUG_ON() in notify_cpu_starting(). In future, either this
         * notifier/enabling capabilities should be fixed to always return
         * success/void or notify_cpu_starting() and other common code should be
         * fixed to expect an error at CPU_STARTING phase.
         */
        ASSERT(system_state != SYS_STATE_boot);
        rc = enable_nonboot_cpu_caps(arm_errata);
        break;
    default:
        break;
    }

    return notifier_from_errno(rc);
}

static struct notifier_block cpu_errata_nfb = {
    .notifier_call = cpu_errata_callback,
};

static int __init cpu_errata_notifier_init(void)
{
    register_cpu_notifier(&cpu_errata_nfb);

    return 0;
}
/*
 * Initialization has to be done at init rather than presmp_init phase because
 * the callback should execute only after the secondary CPUs are initially
 * booted (in hotplug scenarios when the system state is not boot). On boot,
 * the enabling of errata workarounds will be triggered by the boot CPU from
 * start_prtos().
 */
__initcall(cpu_errata_notifier_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: cpuerrata.c === */
/* === BEGIN INLINED: cpufeature.c === */
#include <prtos_prtos_config.h>
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Contains CPU feature definitions
 *
 * The following structures have been imported directly from Linux kernel and
 * should be kept in sync.
 * The current version has been imported from arch/arm64/kernel/cpufeature.c
 *  from kernel version 5.13-rc5 together with the required structures and
 *  macros from arch/arm64/include/asm/cpufeature.h which are stored in
 *  include/asm-arm/arm64/cpufeature.h
 *
 * Copyright (C) 2021 Arm Ltd.
 * based on code from the Linux kernel, which is:
 *  Copyright (C) 2015 ARM Ltd.
 *
 * A note for the weary kernel hacker: the code here is confusing and hard to
 * follow! That's partly because it's solving a nasty problem, but also because
 * there's a little bit of over-abstraction that tends to obscure what's going
 * on behind a maze of helper functions and macros.
 *
 * The basic problem is that hardware folks have started gluing together CPUs
 * with distinct architectural features; in some cases even creating SoCs where
 * user-visible instructions are available only on a subset of the available
 * cores. We try to address this by snapshotting the feature registers of the
 * boot CPU and comparing these with the feature registers of each secondary
 * CPU when bringing them up. If there is a mismatch, then we update the
 * snapshot state to indicate the lowest-common denominator of the feature,
 * known as the "safe" value. This snapshot state can be queried to view the
 * "sanitised" value of a feature register.
 *
 * The sanitised register values are used to decide which capabilities we
 * have in the system. These may be in the form of traditional "hwcaps"
 * advertised to userspace or internal "cpucaps" which are used to configure
 * things like alternative patching and static keys. While a feature mismatch
 * may result in a TAINT_CPU_OUT_OF_SPEC kernel taint, a capability mismatch
 * may prevent a CPU from being onlined at all.
 *
 * Some implementation details worth remembering:
 *
 * - Mismatched features are *always* sanitised to a "safe" value, which
 *   usually indicates that the feature is not supported.
 *
 * - A mismatched feature marked with FTR_STRICT will cause a "SANITY CHECK"
 *   warning when onlining an offending CPU and the kernel will be tainted
 *   with TAINT_CPU_OUT_OF_SPEC.
 *
 * - Features marked as FTR_VISIBLE have their sanitised value visible to
 *   userspace. FTR_VISIBLE features in registers that are only visible
 *   to EL0 by trapping *must* have a corresponding HWCAP so that late
 *   onlining of CPUs cannot lead to features disappearing at runtime.
 *
 * - A "feature" is typically a 4-bit register field. A "capability" is the
 *   high-level description derived from the sanitised field value.
 *
 * - Read the Arm ARM (DDI 0487F.a) section D13.1.3 ("Principles of the ID
 *   scheme for fields in ID registers") to understand when feature fields
 *   may be signed or unsigned (FTR_SIGNED and FTR_UNSIGNED accordingly).
 *
 * - KVM exposes its own view of the feature registers to guest operating
 *   systems regardless of FTR_VISIBLE. This is typically driven from the
 *   sanitised register values to allow virtual CPUs to be migrated between
 *   arbitrary physical CPUs, but some features not present on the host are
 *   also advertised and emulated. Look at sys_reg_descs[] for the gory
 *   details.
 *
 * - If the arm64_ftr_bits[] for a register has a missing field, then this
 *   field is treated as STRICT RES0, including for read_sanitised_ftr_reg().
 *   This is stronger than FTR_HIDDEN and can be used to hide features from
 *   KVM guests.
 */

#include <prtos_bug.h>
#include <prtos_types.h>
#include <prtos_kernel.h>
#include <asm_sysregs.h>
#include <asm_cpufeature.h>
#include <asm_arm64_cpufeature.h>

#define __ARM64_FTR_BITS(SIGNED, VISIBLE, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	{						\
		.sign = (SIGNED),				\
		.visible = (VISIBLE),			\
		.strict = (STRICT),			\
		.type = (TYPE),				\
		.shift = (SHIFT),				\
		.width = (WIDTH),				\
		.safe_val = (SAFE_VAL),			\
	}

/* Define a feature with unsigned values */
#define ARM64_FTR_BITS(VISIBLE, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	__ARM64_FTR_BITS(FTR_UNSIGNED, VISIBLE, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL)

/* Define a feature with a signed value */
#define S_ARM64_FTR_BITS(VISIBLE, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	__ARM64_FTR_BITS(FTR_SIGNED, VISIBLE, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL)

#define ARM64_FTR_END					\
	{						\
		.width = 0,				\
	}

/*
 * NOTE: Any changes to the visibility of features should be kept in
 * sync with the documentation of the CPU feature register ABI.
 */
static const struct arm64_ftr_bits ftr_id_aa64isar0[] = {
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_RNDR_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_TLB_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_TS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_FHM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_DP_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SM4_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SM3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SHA3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_RDM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_ATOMICS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_CRC32_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SHA2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SHA1_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_AES_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64isar1[] = {
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_I8MM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_DGH_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_BF16_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_SPECRES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_SB_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_FRINTTS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_PTR_AUTH),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_GPI_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_PTR_AUTH),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_GPA_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_LRCPC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_FCMA_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_JSCVT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_PTR_AUTH),
		       FTR_STRICT, FTR_EXACT, ID_AA64ISAR1_API_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_PTR_AUTH),
		       FTR_STRICT, FTR_EXACT, ID_AA64ISAR1_APA_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR1_DPB_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64isar2[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_HIGHER_SAFE, ID_AA64ISAR2_CLEARBHB_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_PTR_AUTH),
		       FTR_STRICT, FTR_EXACT, ID_AA64ISAR2_APA3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_PTR_AUTH),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR2_GPA3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64ISAR2_RPRES_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64pfr0[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_CSV3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_CSV2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_DIT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_AMU_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_MPAM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_SEL2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
				   FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_SVE_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_RAS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_GIC_SHIFT, 4, 0),
	S_ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_ASIMD_SHIFT, 4, ID_AA64PFR0_ASIMD_NI),
	S_ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_FP_SHIFT, 4, ID_AA64PFR0_FP_NI),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_EL3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_EL2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_EL1_SHIFT, 4, ID_AA64PFR0_ELx_64BIT_ONLY),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR0_EL0_SHIFT, 4, ID_AA64PFR0_ELx_64BIT_ONLY),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64pfr1[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR1_MPAMFRAC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR1_RASFRAC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_MTE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR1_MTE_SHIFT, 4, ID_AA64PFR1_MTE_NI),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64PFR1_SSBS_SHIFT, 4, ID_AA64PFR1_SSBS_PSTATE_NI),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_BTI),
				    FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR1_BT_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64zfr0[] = {
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_F64MM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_F32MM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_I8MM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_SM4_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_SHA3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_BF16_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_BITPERM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_AES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE_IF_IS_ENABLED(CONFIG_ARM64_SVE),
		       FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ZFR0_SVEVER_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64mmfr0[] = {
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_ECV_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_FGT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_EXS_SHIFT, 4, 0),
	/*
	 * Page size not being supported at Stage-2 is not fatal. You
	 * just give up KVM if PAGE_SIZE isn't supported there. Go fix
	 * your favourite nesting hypervisor.
	 *
	 * There is a small corner case where the hypervisor explicitly
	 * advertises a given granule size at Stage-2 (value 2) on some
	 * vCPUs, and uses the fallback to Stage-1 (value 0) for other
	 * vCPUs. Although this is not forbidden by the architecture, it
	 * indicates that the hypervisor is being silly (or buggy).
	 *
	 * We make no effort to cope with this and pretend that if these
	 * fields are inconsistent across vCPUs, then it isn't worth
	 * trying to bring KVM up.
	 */
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN4_2_SHIFT, 4, 1),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN64_2_SHIFT, 4, 1),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN16_2_SHIFT, 4, 1),
	/*
	 * We already refuse to boot CPUs that don't support our configured
	 * page size, so we can only detect mismatches for a page size other
	 * than the one we're currently using. Unfortunately, SoCs like this
	 * exist in the wild so, even though we don't like it, we'll have to go
	 * along with it and treat them as non-strict.
	 */
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_TGRAN4_SHIFT, 4, ID_AA64MMFR0_TGRAN4_NI),
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_TGRAN64_SHIFT, 4, ID_AA64MMFR0_TGRAN64_NI),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_TGRAN16_SHIFT, 4, ID_AA64MMFR0_TGRAN16_NI),

	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_BIGENDEL0_SHIFT, 4, 0),
	/* Linux shouldn't care about secure memory */
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_SNSMEM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_BIGENDEL_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_ASID_SHIFT, 4, 0),
	/*
	 * Differing PARange is fine as long as all peripherals and memory are mapped
	 * within the minimum PARange of all CPUs
	 */
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_PARANGE_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64mmfr1[] = {
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_AFP_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_ETS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_TWED_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_XNX_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_HIGHER_SAFE, ID_AA64MMFR1_SPECSEI_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_PAN_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_LOR_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_HPD_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_VHE_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_VMIDBITS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_HADBS_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64mmfr2[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_E0PD_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_EVT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_BBM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_TTL_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_FWB_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_IDS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_AT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_ST_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_NV_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_CCIDX_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_LVA_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_IESB_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_LSM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_UAO_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR2_CNP_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_ctr[] = {
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_EXACT, 31, 1, 1), /* RES1 */
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, CTR_DIC_SHIFT, 1, 1),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, CTR_IDC_SHIFT, 1, 1),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_HIGHER_OR_ZERO_SAFE, CTR_CWG_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_HIGHER_OR_ZERO_SAFE, CTR_ERG_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, CTR_DMINLINE_SHIFT, 4, 1),
	/*
	 * Linux can handle differing I-cache policies. Userspace JITs will
	 * make use of *minLine.
	 * If we have differing I-cache policies, report it as the weakest - VIPT.
	 */
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_NONSTRICT, FTR_EXACT, CTR_L1IP_SHIFT, 2, ICACHE_POLICY_VIPT),	/* L1Ip */
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, CTR_IMINLINE_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_mmfr0[] = {
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR0_INNERSHR_SHIFT, 4, 0xf),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR0_FCSE_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_MMFR0_AUXREG_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR0_TCM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR0_SHARELVL_SHIFT, 4, 0),
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR0_OUTERSHR_SHIFT, 4, 0xf),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR0_PMSA_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR0_VMSA_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64dfr0[] = {
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_DOUBLELOCK_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64DFR0_PMSVER_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_CTX_CMPS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_WRPS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_BRPS_SHIFT, 4, 0),
	/*
	 * We can instantiate multiple PMU instances with different levels
	 * of support.
	 */
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_EXACT, ID_AA64DFR0_PMUVER_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_EXACT, ID_AA64DFR0_DEBUGVER_SHIFT, 4, 0x6),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_mvfr2[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, MVFR2_FPMISC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, MVFR2_SIMDMISC_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_dczid[] = {
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_EXACT, DCZID_DZP_SHIFT, 1, 1),
	ARM64_FTR_BITS(FTR_VISIBLE, FTR_STRICT, FTR_LOWER_SAFE, DCZID_BS_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_isar0[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR0_DIVIDE_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR0_DEBUG_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR0_COPROC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR0_CMPBRANCH_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR0_BITFIELD_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR0_BITCOUNT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR0_SWAP_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_isar5[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR5_RDM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR5_CRC32_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR5_SHA2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR5_SHA1_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR5_AES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR5_SEVL_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_mmfr4[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR4_EVT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR4_CCIDX_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR4_LSM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR4_HPDS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR4_CNP_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR4_XNX_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR4_AC2_SHIFT, 4, 0),

	/*
	 * SpecSEI = 1 indicates that the PE might generate an SError on an
	 * external abort on speculative read. It is safe to assume that an
	 * SError might be generated than it will not be. Hence it has been
	 * classified as FTR_HIGHER_SAFE.
	 */
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_HIGHER_SAFE, ID_MMFR4_SPECSEI_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_isar4[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_SWP_FRAC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_PSR_M_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_SYNCH_PRIM_FRAC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_BARRIER_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_SMC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_WRITEBACK_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_WITHSHIFTS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR4_UNPRIV_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_mmfr5[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_MMFR5_ETS_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_isar6[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR6_I8MM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR6_BF16_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR6_SPECRES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR6_SB_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR6_FHM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR6_DP_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_ISAR6_JSCVT_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_pfr0[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR0_DIT_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_PFR0_CSV2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR0_STATE3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR0_STATE2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR0_STATE1_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR0_STATE0_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_pfr1[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_GIC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_VIRT_FRAC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_SEC_FRAC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_GENTIMER_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_VIRTUALIZATION_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_MPROGMOD_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_SECURITY_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_PFR1_PROGMOD_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_pfr2[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_PFR2_SSBS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE, ID_PFR2_CSV3_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_dfr0[] = {
	/* [31:28] TraceFilt */
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR0_PERFMON_SHIFT, 4, 0xf),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR0_MPROFDBG_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR0_MMAPTRC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR0_COPTRC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR0_MMAPDBG_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR0_COPSDBG_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR0_COPDBG_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_dfr1[] = {
	S_ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, ID_DFR1_MTPMU_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_zcr[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_NONSTRICT, FTR_LOWER_SAFE,
		ZCR_ELx_LEN_SHIFT, ZCR_ELx_LEN_SIZE, 0),	/* LEN */
	ARM64_FTR_END,
};

/*
 * Common ftr bits for a 32bit register with all hidden, strict
 * attributes, with 4bit feature fields and a default safe value of
 * 0. Covers the following 32bit registers:
 * id_isar[1-4], id_mmfr[1-3], id_pfr1, mvfr[0-1]
 */
static const struct arm64_ftr_bits ftr_generic_32bits[] = {
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 28, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 24, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 20, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 16, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 12, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 8, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 4, 4, 0),
	ARM64_FTR_BITS(FTR_HIDDEN, FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_raz[] = {
	ARM64_FTR_END,
};

static u64 arm64_ftr_set_value(const struct arm64_ftr_bits *ftrp, s64 reg,
			       s64 ftr_val)
{
	u64 mask = arm64_ftr_mask(ftrp);

	reg &= ~mask;
	reg |= (ftr_val << ftrp->shift) & mask;
	return reg;
}

static s64 arm64_ftr_safe_value(const struct arm64_ftr_bits *ftrp, s64 new,
				s64 cur)
{
	s64 ret = 0;

	switch (ftrp->type) {
	case FTR_EXACT:
		ret = ftrp->safe_val;
		break;
	case FTR_LOWER_SAFE:
		ret = min(new, cur);
		break;
	case FTR_HIGHER_OR_ZERO_SAFE:
		if (!cur || !new)
			break;
		fallthrough;
	case FTR_HIGHER_SAFE:
		ret = max(new, cur);
		break;
	default:
		BUG();
	}

	return ret;
}

/*
 * End of imported linux structures and code
 */

static void sanitize_reg(u64 *cur_reg, u64 new_reg, const char *reg_name,
						const struct arm64_ftr_bits *ftrp)
{
	int taint = 0;
	u64 old_reg = *cur_reg;

	for (;ftrp->width != 0;ftrp++)
	{
		s64 cur_field = arm64_ftr_value(ftrp, *cur_reg);
		s64 new_field = arm64_ftr_value(ftrp, new_reg);

		if (cur_field == new_field)
			continue;

		if (ftrp->strict)
			taint = 1;

		*cur_reg = arm64_ftr_set_value(ftrp, *cur_reg,
							arm64_ftr_safe_value(ftrp, new_field, cur_field));
	}

	if (old_reg != new_reg)
		printk(PRTOSLOG_DEBUG "SANITY DIF: %s 0x%"PRIx64" -> 0x%"PRIx64"\n",
				reg_name, old_reg, new_reg);
	if (old_reg != *cur_reg)
		printk(PRTOSLOG_DEBUG "SANITY FIX: %s 0x%"PRIx64" -> 0x%"PRIx64"\n",
				reg_name, old_reg, *cur_reg);

	if (taint)
	{
		printk(PRTOSLOG_WARNING "SANITY CHECK: Unexpected variation in %s.\n",
				reg_name);
		add_taint(TAINT_CPU_OUT_OF_SPEC);
	}
}


/*
 * This function should be called on secondary cores to sanitize the boot cpu
 * cpuinfo.
 */
void update_system_features(const struct cpuinfo_arm *new)
{

#define SANITIZE_REG(field, num, reg)  \
	sanitize_reg(&system_cpuinfo.field.bits[num], new->field.bits[num], \
				 #reg, ftr_##reg)

#define SANITIZE_ID_REG(field, num, reg)  \
	sanitize_reg(&system_cpuinfo.field.bits[num], new->field.bits[num], \
				#reg, ftr_id_##reg)

#define SANITIZE_RAZ_REG(field, num, reg)  \
	sanitize_reg(&system_cpuinfo.field.bits[num], new->field.bits[num], \
				#reg, ftr_raz)

#define SANITIZE_GENERIC_REG(field, num, reg)  \
	sanitize_reg(&system_cpuinfo.field.bits[num], new->field.bits[num], \
				#reg, ftr_generic_32bits)

	SANITIZE_ID_REG(pfr64, 0, aa64pfr0);
	SANITIZE_ID_REG(pfr64, 1, aa64pfr1);

	SANITIZE_ID_REG(dbg64, 0, aa64dfr0);
	SANITIZE_RAZ_REG(dbg64, 1, aa64dfr1);

	SANITIZE_ID_REG(mm64, 0, aa64mmfr0);
	SANITIZE_ID_REG(mm64, 1, aa64mmfr1);
	SANITIZE_ID_REG(mm64, 2, aa64mmfr2);

	SANITIZE_ID_REG(isa64, 0, aa64isar0);
	SANITIZE_ID_REG(isa64, 1, aa64isar1);
	SANITIZE_ID_REG(isa64, 2, aa64isar2);

	SANITIZE_ID_REG(zfr64, 0, aa64zfr0);

	if ( cpu_has_sve )
		SANITIZE_REG(zcr64, 0, zcr);

	/*
	 * Comment from Linux:
	 * Userspace may perform DC ZVA instructions. Mismatched block sizes
	 * could result in too much or too little memory being zeroed if a
	 * process is preempted and migrated between CPUs.
	 *
	 * ftr_dczid is using STRICT comparison so we will taint PRTOS if different
	 * values are found.
	 */
	SANITIZE_REG(dczid, 0, dczid);

	SANITIZE_REG(ctr, 0, ctr);

	if ( cpu_feature64_has_el0_32(&system_cpuinfo) )
	{
		SANITIZE_ID_REG(pfr32, 0, pfr0);
		SANITIZE_ID_REG(pfr32, 1, pfr1);
		SANITIZE_ID_REG(pfr32, 2, pfr2);

		SANITIZE_ID_REG(dbg32, 0, dfr0);
		SANITIZE_ID_REG(dbg32, 1, dfr1);

		SANITIZE_ID_REG(mm32, 0, mmfr0);
		SANITIZE_GENERIC_REG(mm32, 1, mmfr1);
		SANITIZE_GENERIC_REG(mm32, 2, mmfr2);
		SANITIZE_GENERIC_REG(mm32, 3, mmfr3);
		SANITIZE_ID_REG(mm32, 4, mmfr4);
		SANITIZE_ID_REG(mm32, 5, mmfr5);

		SANITIZE_ID_REG(isa32, 0, isar0);
		SANITIZE_GENERIC_REG(isa32, 1, isar1);
		SANITIZE_GENERIC_REG(isa32, 2, isar2);
		SANITIZE_GENERIC_REG(isa32, 3, isar3);
		SANITIZE_ID_REG(isa32, 4, isar4);
		SANITIZE_ID_REG(isa32, 5, isar5);
		SANITIZE_ID_REG(isa32, 6, isar6);

		SANITIZE_GENERIC_REG(mvfr, 0, mvfr0);
		SANITIZE_GENERIC_REG(mvfr, 1, mvfr1);
#ifndef MVFR2_MAYBE_UNDEFINED
		SANITIZE_REG(mvfr, 2, mvfr2);
#endif
	}
}

/* === END INLINED: cpufeature.c === */
/* === BEGIN INLINED: decode.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/decode.c
 *
 * Instruction decoder
 *
 * Julien Grall <julien.grall@linaro.org>
 * Copyright (C) 2013 Linaro Limited.
 */

#include <prtos_guest_access.h>
#include <prtos_lib.h>
#include <prtos_sched.h>
#include <prtos_types.h>

#include <asm_current.h>

#include "decode.h"

static void update_dabt(struct hsr_dabt *dabt, int reg,
                        uint8_t size, bool sign)
{
    dabt->reg = reg;
    dabt->size = size;
    dabt->sign = sign;
}

static int decode_thumb2(register_t pc, struct hsr_dabt *dabt, uint16_t hw1)
{
    uint16_t hw2;
    uint16_t rt;

    if ( raw_copy_from_guest(&hw2, (void *__user)(pc + 2), sizeof (hw2)) )
        return -EFAULT;

    rt = (hw2 >> 12) & 0xf;

    switch ( (hw1 >> 9) & 0xf )
    {
    case 12:
    {
        bool sign = (hw1 & (1u << 8));
        bool load = (hw1 & (1u << 4));

        if ( (hw1 & 0x0110) == 0x0100 )
            /* NEON instruction */
            goto bad_thumb2;

        if ( (hw1 & 0x0070) == 0x0070 )
            /* Undefined opcodes */
            goto bad_thumb2;

        /* Store/Load single data item */
        if ( rt == 15 )
            /* XXX: Rt == 15 is only invalid for store instruction */
            goto bad_thumb2;

        if ( !load && sign )
            /* Store instruction doesn't support sign extension */
            goto bad_thumb2;

        update_dabt(dabt, rt, (hw1 >> 5) & 3, sign);

        break;
    }
    default:
        goto bad_thumb2;
    }

    return 0;

bad_thumb2:
    gprintk(PRTOSLOG_ERR, "unhandled THUMB2 instruction 0x%x%x\n", hw1, hw2);

    return 1;
}

static int decode_arm64(register_t pc, mmio_info_t *info)
{
    union instr opcode = {0};
    struct hsr_dabt *dabt = &info->dabt;
    struct instr_details *dabt_instr = &info->dabt_instr;

    if ( raw_copy_from_guest(&opcode.value, (void * __user)pc, sizeof (opcode)) )
    {
        gprintk(PRTOSLOG_ERR, "Could not copy the instruction from PC\n");
        return 1;
    }

    /*
     * Refer Arm v8 ARM DDI 0487G.b, Page - C6-1107
     * "Shared decode for all encodings" (under ldr immediate)
     * If n == t && n != 31, then the return value is implementation defined
     * (can be WBSUPPRESS, UNKNOWN, UNDEFINED or NOP). Thus, we do not support
     * this. This holds true for ldrb/ldrh immediate as well.
     *
     * Also refer, Page - C6-1384, the above described behaviour is same for
     * str immediate. This holds true for strb/strh immediate as well
     */
    if ( (opcode.ldr_str.rn == opcode.ldr_str.rt) && (opcode.ldr_str.rn != 31) )
    {
        gprintk(PRTOSLOG_ERR, "Rn should not be equal to Rt except for r31\n");
        goto bad_loadstore;
    }

    /* First, let's check for the fixed values */
    if ( (opcode.value & POST_INDEX_FIXED_MASK) != POST_INDEX_FIXED_VALUE )
    {
        gprintk(PRTOSLOG_ERR,
                "Decoding instruction 0x%x is not supported\n", opcode.value);
        goto bad_loadstore;
    }

    if ( opcode.ldr_str.v != 0 )
    {
        gprintk(PRTOSLOG_ERR,
                "ldr/str post indexing for vector types are not supported\n");
        goto bad_loadstore;
    }

    /* Check for STR (immediate) */
    if ( opcode.ldr_str.opc == 0 )
        dabt->write = 1;
    /* Check for LDR (immediate) */
    else if ( opcode.ldr_str.opc == 1 )
        dabt->write = 0;
    else
    {
        gprintk(PRTOSLOG_ERR,
                "Decoding ldr/str post indexing is not supported for this variant\n");
        goto bad_loadstore;
    }

    gprintk(PRTOSLOG_INFO,
            "opcode->ldr_str.rt = 0x%x, opcode->ldr_str.size = 0x%x, opcode->ldr_str.imm9 = %d\n",
            opcode.ldr_str.rt, opcode.ldr_str.size, opcode.ldr_str.imm9);

    update_dabt(dabt, opcode.ldr_str.rt, opcode.ldr_str.size, false);

    dabt_instr->state = INSTR_LDR_STR_POSTINDEXING;
    dabt_instr->rn = opcode.ldr_str.rn;
    dabt_instr->imm9 = opcode.ldr_str.imm9;
    dabt->valid = 1;

    return 0;

 bad_loadstore:
    gprintk(PRTOSLOG_ERR, "unhandled Arm instruction 0x%x\n", opcode.value);
    return 1;
}

static int decode_thumb(register_t pc, struct hsr_dabt *dabt)
{
    uint16_t instr;

    if ( raw_copy_from_guest(&instr, (void * __user)pc, sizeof (instr)) )
        return -EFAULT;

    switch ( instr >> 12 )
    {
    case 5:
    {
        /* Load/Store register */
        uint16_t opB = (instr >> 9) & 0x7;
        int reg = instr & 7;

        switch ( opB & 0x3 )
        {
        case 0: /* Non-signed word */
            update_dabt(dabt, reg, 2, false);
            break;
        case 1: /* Non-signed halfword */
            update_dabt(dabt, reg, 1, false);
            break;
        case 2: /* Non-signed byte */
            update_dabt(dabt, reg, 0, false);
            break;
        case 3: /* Signed byte */
            update_dabt(dabt, reg, 0, true);
            break;
        }

        break;
    }
    case 6:
        /* Load/Store word immediate offset */
        update_dabt(dabt, instr & 7, 2, false);
        break;
    case 7:
        /* Load/Store byte immediate offset */
        update_dabt(dabt, instr & 7, 0, false);
        break;
    case 8:
        /* Load/Store halfword immediate offset */
        update_dabt(dabt, instr & 7, 1, false);
        break;
    case 9:
        /* Load/Store word sp offset */
        update_dabt(dabt, (instr >> 8) & 7, 2, false);
        break;
    case 14:
        if ( instr & (1 << 11) )
            return decode_thumb2(pc, dabt, instr);
        goto bad_thumb;
    case 15:
        return decode_thumb2(pc, dabt, instr);
    default:
        goto bad_thumb;
    }

    return 0;

bad_thumb:
    gprintk(PRTOSLOG_ERR, "unhandled THUMB instruction 0x%x\n", instr);
    return 1;
}

int decode_instruction(const struct cpu_user_regs *regs, mmio_info_t *info)
{
    if ( is_32bit_domain(current->domain) && regs->cpsr & PSR_THUMB )
        return decode_thumb(regs->pc, &info->dabt);

    if ( !regs_mode_is_32bit(regs) )
        return decode_arm64(regs->pc, info);

    /* TODO: Handle ARM instruction */
    gprintk(PRTOSLOG_ERR, "unhandled ARM instruction\n");

    return 1;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: decode.c === */
/* === BEGIN INLINED: insn.c === */
#include <prtos_prtos_config.h>
/*
 * Based on Linux v4.6 arch/arm64/kernel.ins.c
 *
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Copyright (C) 2014-2016 Zi Shen Lim <zlim.lnx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <prtos_bug.h>
#include <prtos_types.h>
#include <prtos_lib.h>
#include <prtos_errno.h>
#include <prtos_sizes.h>
#include <prtos_bitops.h>
#include <asm_insn.h>
#include <asm_arm64_insn.h>

#define __kprobes
#define pr_err(fmt, ...) printk(PRTOSLOG_ERR fmt, ## __VA_ARGS__)


static int __kprobes aarch64_get_imm_shift_mask(enum aarch64_insn_imm_type type,
						u32 *maskp, int *shiftp)
{
	u32 mask;
	int shift;

	switch (type) {
	case AARCH64_INSN_IMM_26:
		mask = BIT(26, UL) - 1;
		shift = 0;
		break;
	case AARCH64_INSN_IMM_19:
		mask = BIT(19, UL) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_16:
		mask = BIT(16, UL) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_14:
		mask = BIT(14, UL) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_12:
		mask = BIT(12, UL) - 1;
		shift = 10;
		break;
	case AARCH64_INSN_IMM_9:
		mask = BIT(9, UL) - 1;
		shift = 12;
		break;
	case AARCH64_INSN_IMM_7:
		mask = BIT(7, UL) - 1;
		shift = 15;
		break;
	case AARCH64_INSN_IMM_6:
	case AARCH64_INSN_IMM_S:
		mask = BIT(6, UL) - 1;
		shift = 10;
		break;
	case AARCH64_INSN_IMM_R:
		mask = BIT(6, UL) - 1;
		shift = 16;
		break;
	default:
		return -EINVAL;
	}

	*maskp = mask;
	*shiftp = shift;

	return 0;
}

#define ADR_IMM_HILOSPLIT	2
#define ADR_IMM_SIZE		SZ_2M
#define ADR_IMM_LOMASK		((1 << ADR_IMM_HILOSPLIT) - 1)
#define ADR_IMM_HIMASK		((ADR_IMM_SIZE >> ADR_IMM_HILOSPLIT) - 1)
#define ADR_IMM_LOSHIFT		29
#define ADR_IMM_HISHIFT		5


u32 __kprobes aarch64_insn_encode_immediate(enum aarch64_insn_imm_type type,
				  u32 insn, u64 imm)
{
	u32 immlo, immhi, mask;
	int shift;

	if (insn == AARCH64_BREAK_FAULT)
		return AARCH64_BREAK_FAULT;

	switch (type) {
	case AARCH64_INSN_IMM_ADR:
		shift = 0;
		immlo = (imm & ADR_IMM_LOMASK) << ADR_IMM_LOSHIFT;
		imm >>= ADR_IMM_HILOSPLIT;
		immhi = (imm & ADR_IMM_HIMASK) << ADR_IMM_HISHIFT;
		imm = immlo | immhi;
		mask = ((ADR_IMM_LOMASK << ADR_IMM_LOSHIFT) |
			(ADR_IMM_HIMASK << ADR_IMM_HISHIFT));
		break;
	default:
		if (aarch64_get_imm_shift_mask(type, &mask, &shift) < 0) {
			pr_err("aarch64_insn_encode_immediate: unknown immediate encoding %d\n",
			       type);
			return AARCH64_BREAK_FAULT;
		}
	}

	/* Update the immediate field. */
	insn &= ~(mask << shift);
	insn |= (imm & mask) << shift;

	return insn;
}

static inline long branch_imm_common(unsigned long pc, unsigned long addr,
				     long range)
{
	long offset;

	if ((pc & 0x3) || (addr & 0x3)) {
		pr_err("%s: A64 instructions must be word aligned\n", __func__);
		return range;
	}

	offset = ((long)addr - (long)pc);

	if (offset < -range || offset >= range) {
		pr_err("%s: offset out of range\n", __func__);
		return range;
	}

	return offset;
}


u32 __kprobes aarch64_insn_gen_hint(enum aarch64_insn_hint_op op)
{
	return aarch64_insn_get_hint_value() | op;
}

u32 __kprobes aarch64_insn_gen_nop(void)
{
	return aarch64_insn_gen_hint(AARCH64_INSN_HINT_NOP);
}



/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */

/* === END INLINED: insn.c === */
/* === BEGIN INLINED: vfp.c === */
#include <prtos_prtos_config.h>
#include <prtos_sched.h>
#include <asm_processor.h>
#include <asm_cpufeature.h>
#include <asm_vfp.h>
#include <asm_arm64_sve.h>

static inline void save_state(uint64_t *fpregs)
{
    asm volatile("stp q0, q1, [%1, #16 * 0]\n\t"
                 "stp q2, q3, [%1, #16 * 2]\n\t"
                 "stp q4, q5, [%1, #16 * 4]\n\t"
                 "stp q6, q7, [%1, #16 * 6]\n\t"
                 "stp q8, q9, [%1, #16 * 8]\n\t"
                 "stp q10, q11, [%1, #16 * 10]\n\t"
                 "stp q12, q13, [%1, #16 * 12]\n\t"
                 "stp q14, q15, [%1, #16 * 14]\n\t"
                 "stp q16, q17, [%1, #16 * 16]\n\t"
                 "stp q18, q19, [%1, #16 * 18]\n\t"
                 "stp q20, q21, [%1, #16 * 20]\n\t"
                 "stp q22, q23, [%1, #16 * 22]\n\t"
                 "stp q24, q25, [%1, #16 * 24]\n\t"
                 "stp q26, q27, [%1, #16 * 26]\n\t"
                 "stp q28, q29, [%1, #16 * 28]\n\t"
                 "stp q30, q31, [%1, #16 * 30]\n\t"
                 : "=Q" (*fpregs) : "r" (fpregs));
}

static inline void restore_state(const uint64_t *fpregs)
{
    asm volatile("ldp q0, q1, [%1, #16 * 0]\n\t"
                 "ldp q2, q3, [%1, #16 * 2]\n\t"
                 "ldp q4, q5, [%1, #16 * 4]\n\t"
                 "ldp q6, q7, [%1, #16 * 6]\n\t"
                 "ldp q8, q9, [%1, #16 * 8]\n\t"
                 "ldp q10, q11, [%1, #16 * 10]\n\t"
                 "ldp q12, q13, [%1, #16 * 12]\n\t"
                 "ldp q14, q15, [%1, #16 * 14]\n\t"
                 "ldp q16, q17, [%1, #16 * 16]\n\t"
                 "ldp q18, q19, [%1, #16 * 18]\n\t"
                 "ldp q20, q21, [%1, #16 * 20]\n\t"
                 "ldp q22, q23, [%1, #16 * 22]\n\t"
                 "ldp q24, q25, [%1, #16 * 24]\n\t"
                 "ldp q26, q27, [%1, #16 * 26]\n\t"
                 "ldp q28, q29, [%1, #16 * 28]\n\t"
                 "ldp q30, q31, [%1, #16 * 30]\n\t"
                 : : "Q" (*fpregs), "r" (fpregs));
}

void vfp_save_state(struct vcpu *v)
{
    if ( !cpu_has_fp )
        return;

    if ( is_sve_domain(v->domain) )
        sve_save_state(v);
    else
        save_state(v->arch.vfp.fpregs);

    v->arch.vfp.fpsr = READ_SYSREG(FPSR);
    v->arch.vfp.fpcr = READ_SYSREG(FPCR);
    if ( is_32bit_domain(v->domain) )
        v->arch.vfp.fpexc32_el2 = READ_SYSREG(FPEXC32_EL2);
}

void vfp_restore_state(struct vcpu *v)
{
    if ( !cpu_has_fp )
        return;

    if ( is_sve_domain(v->domain) )
        sve_restore_state(v);
    else
        restore_state(v->arch.vfp.fpregs);

    WRITE_SYSREG(v->arch.vfp.fpsr, FPSR);
    WRITE_SYSREG(v->arch.vfp.fpcr, FPCR);
    if ( is_32bit_domain(v->domain) )
        WRITE_SYSREG(v->arch.vfp.fpexc32_el2, FPEXC32_EL2);
}

/* === END INLINED: vfp.c === */
/* === BEGIN INLINED: guest_atomics.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/guest_atomics.c
 */
#include <prtos_prtos_config.h>

#include <prtos_cpu.h>

#include <asm_guest_atomics.h>

DEFINE_PER_CPU_READ_MOSTLY(unsigned int, guest_safe_atomic_max);

/*
 * Heuristic to find a safe upper-limit for load-store exclusive
 * operations on memory shared with guest.
 *
 * At the moment, we calculate the number of iterations of a simple
 * load-store atomic loop in 1uS.
 */
static void calibrate_safe_atomic(void)
{
    s_time_t deadline = NOW() + MICROSECS(1);
    unsigned int counter = 0;
    unsigned long mem = 0;

    do
    {
        unsigned long res, tmp;

#ifdef CONFIG_ARM_32
        asm volatile (" ldrex   %2, %1\n"
                      " add     %2, %2, #1\n"
                      " strex   %0, %2, %1\n"
                      : "=&r" (res), "+Q" (mem), "=&r" (tmp));
#else
        asm volatile (" ldxr    %w2, %1\n"
                      " add     %w2, %w2, #1\n"
                      " stxr    %w0, %w2, %1\n"
                      : "=&r" (res), "+Q" (mem), "=&r" (tmp));
#endif
        counter++;
    } while (NOW() < deadline);

    this_cpu(guest_safe_atomic_max) = counter;

    printk(PRTOSLOG_DEBUG
           "CPU%u: Guest atomics will try %u times before pausing the domain\n",
           smp_processor_id(), counter);
}

static int cpu_guest_safe_atomic_callback(struct notifier_block *nfb,
                                          unsigned long action,
                                          void *hcpu)
{
    if ( action == CPU_STARTING )
        calibrate_safe_atomic();

    return NOTIFY_DONE;
}

void calibrate_safe_atomic_prtos(void) {
    calibrate_safe_atomic();
}

static struct notifier_block cpu_guest_safe_atomic_nfb = {
    .notifier_call = cpu_guest_safe_atomic_callback,
};

static int __init guest_safe_atomic_init(void)
{
    register_cpu_notifier(&cpu_guest_safe_atomic_nfb);

    calibrate_safe_atomic();

    return 0;
}
presmp_initcall(guest_safe_atomic_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: guest_atomics.c === */
/* === BEGIN INLINED: guest_walk.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Guest page table walk
 * Copyright (c) 2017 Sergej Proskurin <proskurin@sec.in.tum.de>
 */

#include <prtos_domain_page.h>
#include <prtos_guest_access.h>
#include <prtos_sched.h>

#include <asm_guest_walk.h>
#include <asm_short-desc.h>

/*
 * The function guest_walk_sd translates a given GVA into an IPA using the
 * short-descriptor translation table format in software. This function assumes
 * that the domain is running on the currently active vCPU. To walk the guest's
 * page table on a different vCPU, the following registers would need to be
 * loaded: TCR_EL1, TTBR0_EL1, TTBR1_EL1, and SCTLR_EL1.
 */
static bool guest_walk_sd(const struct vcpu *v,
                          vaddr_t gva, paddr_t *ipa,
                          unsigned int *perms)
{
    int ret;
    bool disabled = true;
    uint32_t ttbr;
    paddr_t mask, paddr;
    short_desc_t pte;
    register_t ttbcr = READ_SYSREG(TCR_EL1);
    unsigned int n = ttbcr & TTBCR_N_MASK;
    struct domain *d = v->domain;

    mask = GENMASK_ULL(31, (32 - n));

    if ( n == 0 || !(gva & mask) )
    {
        /*
         * Use TTBR0 for GVA to IPA translation.
         *
         * Note that on AArch32, the TTBR0_EL1 register is 32-bit wide.
         * Nevertheless, we have to use the READ_SYSREG64 macro, as it is
         * required for reading TTBR0_EL1.
         */
        ttbr = READ_SYSREG64(TTBR0_EL1);

        /* If TTBCR.PD0 is set, translations using TTBR0 are disabled. */
        disabled = ttbcr & TTBCR_PD0;
    }
    else
    {
        /*
         * Use TTBR1 for GVA to IPA translation.
         *
         * Note that on AArch32, the TTBR1_EL1 register is 32-bit wide.
         * Nevertheless, we have to use the READ_SYSREG64 macro, as it is
         * required for reading TTBR1_EL1.
         */
        ttbr = READ_SYSREG64(TTBR1_EL1);

        /* If TTBCR.PD1 is set, translations using TTBR1 are disabled. */
        disabled = ttbcr & TTBCR_PD1;

        /*
         * TTBR1 translation always works like n==0 TTBR0 translation (ARM DDI
         * 0487B.a J1-6003).
         */
        n = 0;
    }

    if ( disabled )
        return false;

    /*
     * The address of the L1 descriptor for the initial lookup has the
     * following format: [ttbr<31:14-n>:gva<31-n:20>:00] (ARM DDI 0487B.a
     * J1-6003). Note that the following GPA computation already considers that
     * the first level address translation might comprise up to four
     * consecutive pages and does not need to be page-aligned if n > 2.
     */
    mask = GENMASK(31, (14 - n));
    paddr = (ttbr & mask);

    mask = GENMASK((31 - n), 20);
    paddr |= (gva & mask) >> 18;

    /* Access the guest's memory to read only one PTE. */
    ret = access_guest_memory_by_gpa(d, paddr, &pte, sizeof(short_desc_t), false);
    if ( ret )
        return false;

    switch ( pte.walk.dt )
    {
    case L1DESC_INVALID:
        return false;

    case L1DESC_PAGE_TABLE:
        /*
         * The address of the L2 descriptor has the following format:
         * [l1desc<31:10>:gva<19:12>:00] (ARM DDI 0487B.aJ1-6004). Note that
         * the following address computation already considers that the second
         * level translation table does not need to be page aligned.
         */
        mask = GENMASK(19, 12);
        /*
         * Cast pte.walk.base to paddr_t to cope with C type promotion of types
         * smaller than int. Otherwise pte.walk.base would be casted to int and
         * subsequently sign extended, thus leading to a wrong value.
         */
        paddr = ((paddr_t)pte.walk.base << 10) | ((gva & mask) >> 10);

        /* Access the guest's memory to read only one PTE. */
        ret = access_guest_memory_by_gpa(d, paddr, &pte, sizeof(short_desc_t), false);
        if ( ret )
            return false;

        if ( pte.walk.dt == L2DESC_INVALID )
            return false;

        if ( pte.pg.page ) /* Small page. */
        {
            mask = (1ULL << L2DESC_SMALL_PAGE_SHIFT) - 1;
            *ipa = ((paddr_t)pte.pg.base << L2DESC_SMALL_PAGE_SHIFT) | (gva & mask);

            /* Set execute permissions associated with the small page. */
            if ( !pte.pg.xn )
                *perms |= GV2M_EXEC;
        }
        else /* Large page. */
        {
            mask = (1ULL << L2DESC_LARGE_PAGE_SHIFT) - 1;
            *ipa = ((paddr_t)pte.lpg.base << L2DESC_LARGE_PAGE_SHIFT) | (gva & mask);

            /* Set execute permissions associated with the large page. */
            if ( !pte.lpg.xn )
                *perms |= GV2M_EXEC;
        }

        /* Set permissions so that the caller can check the flags by herself. */
        if ( !pte.pg.ro )
            *perms |= GV2M_WRITE;

        break;

    case L1DESC_SECTION:
    case L1DESC_SECTION_PXN:
        if ( !pte.sec.supersec ) /* Section */
        {
            mask = (1ULL << L1DESC_SECTION_SHIFT) - 1;
            *ipa = ((paddr_t)pte.sec.base << L1DESC_SECTION_SHIFT) | (gva & mask);
        }
        else /* Supersection */
        {
            mask = (1ULL << L1DESC_SUPERSECTION_SHIFT) - 1;
            *ipa = gva & mask;
            *ipa |= (paddr_t)(pte.supersec.base) << L1DESC_SUPERSECTION_SHIFT;
#ifndef CONFIG_PHYS_ADDR_T_32
            *ipa |= (paddr_t)(pte.supersec.extbase1) << L1DESC_SUPERSECTION_EXT_BASE1_SHIFT;
            *ipa |= (paddr_t)(pte.supersec.extbase2) << L1DESC_SUPERSECTION_EXT_BASE2_SHIFT;
#endif /* CONFIG_PHYS_ADDR_T_32 */
        }

        /* Set permissions so that the caller can check the flags by herself. */
        if ( !pte.sec.ro )
            *perms |= GV2M_WRITE;
        if ( !pte.sec.xn )
            *perms |= GV2M_EXEC;

        break;
    }

    return true;
}

/*
 * Get the IPA output_size (configured in TCR_EL1) that shall be used for the
 * long-descriptor based translation table walk.
 */
static int get_ipa_output_size(struct domain *d, register_t tcr,
                               unsigned int *output_size)
{
#ifdef CONFIG_ARM_64
    register_t ips;

    static const unsigned int ipa_sizes[7] = {
        TCR_EL1_IPS_32_BIT_VAL,
        TCR_EL1_IPS_36_BIT_VAL,
        TCR_EL1_IPS_40_BIT_VAL,
        TCR_EL1_IPS_42_BIT_VAL,
        TCR_EL1_IPS_44_BIT_VAL,
        TCR_EL1_IPS_48_BIT_VAL,
        TCR_EL1_IPS_52_BIT_VAL
    };

    if ( is_64bit_domain(d) )
    {
        /* Get the intermediate physical address size. */
        ips = tcr & TCR_EL1_IPS_MASK;

        /*
         * Return an error on reserved IPA output-sizes and if the IPA
         * output-size is 52bit.
         *
         * XXX: 52 bit output-size is not supported yet.
         */
        if ( ips > TCR_EL1_IPS_48_BIT )
            return -EFAULT;

        *output_size = ipa_sizes[ips >> TCR_EL1_IPS_SHIFT];
    }
    else
#endif
        *output_size = TCR_EL1_IPS_40_BIT_VAL;

    return 0;
}

/* Normalized page granule size indices. */
enum granule_size_index {
    GRANULE_SIZE_INDEX_4K,
    GRANULE_SIZE_INDEX_16K,
    GRANULE_SIZE_INDEX_64K
};

/* Represent whether TTBR0 or TTBR1 is active. */
enum active_ttbr {
    TTBR0_ACTIVE,
    TTBR1_ACTIVE
};

/*
 * Select the TTBR(0|1)_EL1 that will be used for address translation using the
 * long-descriptor translation table format and return the page granularity
 * that is used by the selected TTBR. Please note that the TCR.TG0 and TCR.TG1
 * encodings differ.
 */
static bool get_ttbr_and_gran_64bit(uint64_t *ttbr, unsigned int *gran,
                                    register_t tcr, enum active_ttbr ttbrx)
{
    bool disabled;

    if ( ttbrx == TTBR0_ACTIVE )
    {
        /* Normalize granule size. */
        switch ( tcr & TCR_TG0_MASK )
        {
        case TCR_TG0_16K:
            *gran = GRANULE_SIZE_INDEX_16K;
            break;
        case TCR_TG0_64K:
            *gran = GRANULE_SIZE_INDEX_64K;
            break;
        default:
            /*
             * According to ARM DDI 0487B.a D7-2487, if the TCR_EL1.TG0 value
             * is programmed to either a reserved value, or a size that has not
             * been implemented, then the hardware will treat the field as if
             * it has been programmed to an IMPLEMENTATION DEFINED choice.
             *
             * This implementation strongly follows the pseudo-code
             * implementation from ARM DDI 0487B.a J1-5924 which suggests to
             * fall back to 4K by default.
             */
            *gran = GRANULE_SIZE_INDEX_4K;
            break;
        }

        /* Use TTBR0 for GVA to IPA translation. */
        *ttbr = READ_SYSREG64(TTBR0_EL1);

        /* If TCR.EPD0 is set, translations using TTBR0 are disabled. */
        disabled = tcr & TCR_EPD0;
    }
    else
    {
        /* Normalize granule size. */
        switch ( tcr & TCR_EL1_TG1_MASK )
        {
        case TCR_EL1_TG1_16K:
            *gran = GRANULE_SIZE_INDEX_16K;
            break;
        case TCR_EL1_TG1_64K:
            *gran = GRANULE_SIZE_INDEX_64K;
            break;
        default:
            /*
             * According to ARM DDI 0487B.a D7-2486, if the TCR_EL1.TG1 value
             * is programmed to either a reserved value, or a size that has not
             * been implemented, then the hardware will treat the field as if
             * it has been programmed to an IMPLEMENTATION DEFINED choice.
             *
             * This implementation strongly follows the pseudo-code
             * implementation from ARM DDI 0487B.a J1-5924 which suggests to
             * fall back to 4K by default.
             */
            *gran = GRANULE_SIZE_INDEX_4K;
            break;
        }

        /* Use TTBR1 for GVA to IPA translation. */
        *ttbr = READ_SYSREG64(TTBR1_EL1);

        /* If TCR.EPD1 is set, translations using TTBR1 are disabled. */
        disabled = tcr & TCR_EPD1;
    }

    return disabled;
}

/*
 * Get the MSB number of the GVA, according to "AddrTop" pseudocode
 * implementation in ARM DDI 0487B.a J1-6066.
 */
static unsigned int get_top_bit(struct domain *d, vaddr_t gva, register_t tcr)
{
    unsigned int topbit;

    /*
     * If EL1 is using AArch64 then addresses from EL0 using AArch32 are
     * zero-extended to 64 bits (ARM DDI 0487B.a J1-6066).
     */
    if ( is_32bit_domain(d) )
        topbit = 31;
    else
    {
        if ( ((gva & BIT(55, ULL)) && (tcr & TCR_EL1_TBI1)) ||
             (!(gva & BIT(55, ULL)) && (tcr & TCR_EL1_TBI0)) )
            topbit = 55;
        else
            topbit = 63;
    }

    return topbit;
}

/* Make sure the base address does not exceed its configured size. */
static bool check_base_size(unsigned int output_size, uint64_t base)
{
    paddr_t mask = GENMASK_ULL((TCR_EL1_IPS_48_BIT_VAL - 1), output_size);

    if ( (output_size < TCR_EL1_IPS_48_BIT_VAL) && (base & mask) )
        return false;

    return true;
}

/*
 * The function guest_walk_ld translates a given GVA into an IPA using the
 * long-descriptor translation table format in software. This function assumes
 * that the domain is running on the currently active vCPU. To walk the guest's
 * page table on a different vCPU, the following registers would need to be
 * loaded: TCR_EL1, TTBR0_EL1, TTBR1_EL1, and SCTLR_EL1.
 */
static bool guest_walk_ld(const struct vcpu *v,
                          vaddr_t gva, paddr_t *ipa,
                          unsigned int *perms)
{
    int ret;
    bool disabled = true;
    bool ro_table = false, xn_table = false;
    unsigned int t0_sz, t1_sz;
    unsigned int level, gran;
    unsigned int topbit = 0, input_size = 0, output_size;
    uint64_t ttbr = 0;
    paddr_t mask, paddr;
    lpae_t pte;
    register_t tcr = READ_SYSREG(TCR_EL1);
    struct domain *d = v->domain;

    static const unsigned int grainsizes[3] = {
        PAGE_SHIFT_4K,
        PAGE_SHIFT_16K,
        PAGE_SHIFT_64K
    };

    t0_sz = (tcr >> TCR_T0SZ_SHIFT) & TCR_SZ_MASK;
    t1_sz = (tcr >> TCR_T1SZ_SHIFT) & TCR_SZ_MASK;

    /* Get the MSB number of the GVA. */
    topbit = get_top_bit(d, gva, tcr);

    if ( is_64bit_domain(d) )
    {
        /* Select the TTBR(0|1)_EL1 that will be used for address translation. */

        if ( (gva & BIT(topbit, ULL)) == 0 )
        {
            input_size = 64 - t0_sz;

            /* Get TTBR0 and configured page granularity. */
            disabled = get_ttbr_and_gran_64bit(&ttbr, &gran, tcr, TTBR0_ACTIVE);
        }
        else
        {
            input_size = 64 - t1_sz;

            /* Get TTBR1 and configured page granularity. */
            disabled = get_ttbr_and_gran_64bit(&ttbr, &gran, tcr, TTBR1_ACTIVE);
        }

        /*
         * The current implementation supports intermediate physical address
         * sizes (IPS) up to 48 bit.
         *
         * XXX: Determine whether the IPS_MAX_VAL is 48 or 52 in software.
         */
        if ( (input_size > TCR_EL1_IPS_48_BIT_VAL) ||
             (input_size < TCR_EL1_IPS_MIN_VAL) )
            return false;
    }
    else
    {
        /* Granule size of AArch32 architectures is always 4K. */
        gran = GRANULE_SIZE_INDEX_4K;

        /* Select the TTBR(0|1)_EL1 that will be used for address translation. */

        /*
         * Check if the bits <31:32-t0_sz> of the GVA are set to 0 (DDI 0487B.a
         * J1-5999). If so, TTBR0 shall be used for address translation.
         */
        mask = GENMASK_ULL(31, (32 - t0_sz));

        if ( t0_sz == 0 || !(gva & mask) )
        {
            input_size = 32 - t0_sz;

            /* Use TTBR0 for GVA to IPA translation. */
            ttbr = READ_SYSREG64(TTBR0_EL1);

            /* If TCR.EPD0 is set, translations using TTBR0 are disabled. */
            disabled = tcr & TCR_EPD0;
        }

        /*
         * Check if the bits <31:32-t1_sz> of the GVA are set to 1 (DDI 0487B.a
         * J1-6000). If so, TTBR1 shall be used for address translation.
         */
        mask = GENMASK_ULL(31, (32 - t1_sz));

        if ( ((t1_sz == 0) && !ttbr) || (t1_sz && (gva & mask) == mask) )
        {
            input_size = 32 - t1_sz;

            /* Use TTBR1 for GVA to IPA translation. */
            ttbr = READ_SYSREG64(TTBR1_EL1);

            /* If TCR.EPD1 is set, translations using TTBR1 are disabled. */
            disabled = tcr & TCR_EPD1;
        }
    }

    if ( disabled )
        return false;

    /*
     * The starting level is the number of strides (grainsizes[gran] - 3)
     * needed to consume the input address (ARM DDI 0487B.a J1-5924).
     */
    level = 4 - DIV_ROUND_UP((input_size - grainsizes[gran]), (grainsizes[gran] - 3));

    /* Get the IPA output_size. */
    ret = get_ipa_output_size(d, tcr, &output_size);
    if ( ret )
        return false;

    /* Make sure the base address does not exceed its configured size. */
    ret = check_base_size(output_size, ttbr);
    if ( !ret )
        return false;

    /*
     * Compute the base address of the first level translation table that is
     * given by TTBRx_EL1 (ARM DDI 0487B.a D4-2024 and J1-5926).
     */
    mask = GENMASK_ULL(47, grainsizes[gran]);
    paddr = (ttbr & mask);

    for ( ; ; level++ )
    {
        /*
         * Add offset given by the GVA to the translation table base address.
         * Shift the offset by 3 as it is 8-byte aligned.
         */
        paddr |= LPAE_TABLE_INDEX_GS(grainsizes[gran], level, gva) << 3;

        /* Access the guest's memory to read only one PTE. */
        ret = access_guest_memory_by_gpa(d, paddr, &pte, sizeof(lpae_t), false);
        if ( ret )
            return false;

        /* Make sure the base address does not exceed its configured size. */
        ret = check_base_size(output_size, pfn_to_paddr(pte.walk.base));
        if ( !ret )
            return false;

        /*
         * If page granularity is 64K, make sure the address is aligned
         * appropriately.
         */
        if ( (output_size < TCR_EL1_IPS_52_BIT_VAL) &&
             (gran == GRANULE_SIZE_INDEX_64K) &&
             (pte.walk.base & 0xf) )
            return false;

        /*
         * Break if one of the following conditions is true:
         *
         * - We have found the PTE holding the IPA (level == 3).
         * - The PTE is not valid.
         * - If (level < 3) and the PTE is valid, we found a block descriptor.
         */
        if ( level == 3 || !lpae_is_valid(pte) || lpae_is_superpage(pte, level) )
            break;

        /*
         * Temporarily store permissions of the table descriptor as they are
         * inherited by page table attributes (ARM DDI 0487B.a J1-5928).
         */
        xn_table |= pte.pt.xnt;             /* Execute-Never */
        ro_table |= pte.pt.apt & BIT(1, UL);/* Read-Only */

        /* Compute the base address of the next level translation table. */
        mask = GENMASK_ULL(47, grainsizes[gran]);
        paddr = pfn_to_paddr(pte.walk.base) & mask;
    }

    /*
     * According to ARM DDI 0487B.a J1-5927, we return an error if the found
     * PTE is invalid or holds a reserved entry (PTE<1:0> == x0)) or if the PTE
     * maps a memory block at level 3 (PTE<1:0> == 01).
     */
    if ( !lpae_is_valid(pte) || !lpae_is_mapping(pte, level) )
        return false;

    /* Make sure that the lower bits of the PTE's base address are zero. */
    mask = GENMASK_ULL(47, grainsizes[gran]);
    *ipa = (pfn_to_paddr(pte.walk.base) & mask) |
        (gva & (LEVEL_SIZE_GS(grainsizes[gran], level) - 1));

    /*
     * Set permissions so that the caller can check the flags by herself. Note
     * that stage 1 translations also inherit attributes from the tables
     * (ARM DDI 0487B.a J1-5928).
     */
    if ( !pte.pt.ro && !ro_table )
        *perms |= GV2M_WRITE;
    if ( !pte.pt.xn && !xn_table )
        *perms |= GV2M_EXEC;

    return true;
}

bool guest_walk_tables(const struct vcpu *v, vaddr_t gva,
                       paddr_t *ipa, unsigned int *perms)
{
    register_t sctlr = READ_SYSREG(SCTLR_EL1);
    register_t tcr = READ_SYSREG(TCR_EL1);
    unsigned int _perms;

    /* We assume that the domain is running on the currently active domain. */
    if ( v != current )
        return false;

    /* Allow perms to be NULL. */
    perms = perms ?: &_perms;

    /*
     * Currently, we assume a GVA to IPA translation with EL1 privileges.
     * Since, valid mappings in the first stage address translation table are
     * readable by default for EL1, we initialize perms with GV2M_READ and
     * extend the permissions as part of the particular page table walk. Please
     * note that the current implementation does not consider further
     * attributes that distinguish between EL0 and EL1 permissions (EL0 might
     * not have permissions on the particular mapping).
     */
    *perms = GV2M_READ;

    /* If the MMU is disabled, there is no need to translate the gva. */
    if ( !(sctlr & SCTLR_Axx_ELx_M) )
    {
        *ipa = gva;

        /* Memory can be accessed without any restrictions. */
        *perms = GV2M_READ|GV2M_WRITE|GV2M_EXEC;

        return true;
    }

    if ( is_32bit_domain(v->domain) && !(tcr & TTBCR_EAE) )
        return guest_walk_sd(v, gva, ipa, perms);
    else
        return guest_walk_ld(v, gva, ipa, perms);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: guest_walk.c === */
