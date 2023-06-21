/*
 * FILE: mpspec.c
 *
 * Intel multiprocessor specification based table parsing
 *
 * www.prtos.org
 */
#include <arch/paging.h>
#include <assert.h>
#include <boot.h>
#include <sched.h>
#include <smp.h>

#ifdef CONFIG_SMP_INTERFACE_MPSPEC

#define EBDA_BIOS 0x40EUL

static inline prtos_u32_t __VBOOT get_bios_ebda(void) {
    prtos_u32_t address = *(prtos_u16_t *)(EBDA_BIOS);
    address <<= 4;
    return address;
}

static prtos_s32_t __VBOOT mpf_checksum(prtos_u8_t *mp, prtos_s32_t len) {
    prtos_s32_t sum = 0;

    while (len--) sum += *mp++;

    return sum & 0xFF;
}

static struct mp_floating_pointer *__VBOOT mp_scan_config_at(prtos_address_t base, prtos_u_size_t length) {
    prtos_u32_t *bp = (prtos_u32_t *)(base);
    struct mp_floating_pointer *mpf;

    while (length > 0) {
        mpf = (struct mp_floating_pointer *)bp;
        if ((*bp == SMP_MAGIC_IDENT) && (mpf->length == 1) && !mpf_checksum((prtos_u8_t *)bp, 16) && ((mpf->specification == 1) || (mpf->specification == 4))) {
            eprintf(">> SMP MP-table found at 0x%x\n", (mpf));
            return mpf;
        }
        bp += 4;
        length -= 16;
    }
    return 0;
}

static struct mp_floating_pointer *__VBOOT mp_scan_config(void) {
    struct mp_floating_pointer *mpf;

    mpf = NULL;
    if ((mpf = mp_scan_config_at(0x0, 0x400)) || (mpf = mp_scan_config_at(639 * 0x400, 0x400)) || (mpf = mp_scan_config_at(0xf0000, 0x10000)))
        ;

    return mpf;
}

static inline prtos_s32_t mp_add_procceor(void *entry) {
    struct mp_config_processor *m = entry;

    if (m->cpu_flag & CPU_ENABLED) {
        if (x86_mp_conf.num_of_cpu >= CONFIG_NO_CPUS) {
            x86system_panic("Only supported %d cpus\n", CONFIG_NO_CPUS);
        }
        x86_mp_conf.cpu[x86_mp_conf.num_of_cpu].bsp = (m->cpu_flag & CPU_BOOTPROCESSOR) >> 1;
        x86_mp_conf.cpu[x86_mp_conf.num_of_cpu].enabled = m->cpu_flag & CPU_ENABLED;
        x86_mp_conf.cpu[x86_mp_conf.num_of_cpu].id = m->apic_id;
        x86_mp_conf.num_of_cpu++;
    }

    return sizeof(struct mp_config_processor);
}

static inline prtos_s32_t mp_add_bus(void *entry) {
    struct mp_config_bus *m = entry;

    if (x86_mp_conf.num_of_bus >= CONFIG_MAX_NO_BUSES) {
        x86system_panic("Only supported %d buses\n", CONFIG_MAX_NO_BUSES);
    }
    x86_mp_conf.bus[x86_mp_conf.num_of_bus].id = m->bus_id;
    if (strncmp(m->bus_type, BUSTYPE_ISA, strlen(BUSTYPE_ISA)) == 0) {
        x86_mp_conf.bus[x86_mp_conf.num_of_bus].type = MP_BUS_ISA;
        x86_mp_conf.bus[x86_mp_conf.num_of_bus].polarity = BUS_HIGH_POLARITY;
        x86_mp_conf.bus[x86_mp_conf.num_of_bus].trigger_mode = BUS_EDGE_TRIGGER;
    } else if (strncmp(m->bus_type, BUSTYPE_PCI, strlen(BUSTYPE_PCI)) == 0) {
        x86_mp_conf.bus[x86_mp_conf.num_of_bus].type = MP_BUS_PCI;
        x86_mp_conf.bus[x86_mp_conf.num_of_bus].polarity = BUS_LOW_POLARITY;
        x86_mp_conf.bus[x86_mp_conf.num_of_bus].trigger_mode = BUS_LEVEL_TRIGGER;
    } else {
        x86system_panic("Found unknown bus type %s\n", m->bus_type);
    }
    x86_mp_conf.num_of_bus++;

    return sizeof(struct mp_config_bus);
}

static inline prtos_s32_t mp_add_io_apic(void *entry) {
    struct mp_config_io_apic *m = entry;

    if (x86_mp_conf.num_of_io_apic >= CONFIG_MAX_NO_IOAPICS) {
        x86system_panic("Only supported %d IO-Apic\n", CONFIG_MAX_NO_IOAPICS);
    }
    x86_mp_conf.io_apic[x86_mp_conf.num_of_io_apic].id = m->apic_id;
    x86_mp_conf.io_apic[x86_mp_conf.num_of_io_apic].base_addr = m->apic_addr;
    x86_mp_conf.num_of_io_apic++;

    return sizeof(struct mp_config_io_apic);
}

static inline prtos_s32_t mp_add_int_src(void *entry) {
    struct mp_config_int_src *m = entry;

    if (x86_mp_conf.num_of_io_int >= CONFIG_MAX_NO_IOINT) {
        x86system_panic("Only supported %d IO-Irqs", CONFIG_MAX_NO_IOINT);
    }
    x86_mp_conf.io_int[x86_mp_conf.num_of_io_int].irq_type = m->irq_type;
    x86_mp_conf.io_int[x86_mp_conf.num_of_io_int].polarity = m->irq_flag & 0x3;
    x86_mp_conf.io_int[x86_mp_conf.num_of_io_int].trigger_mode = (m->irq_flag >> 2) & 0x3;
    x86_mp_conf.io_int[x86_mp_conf.num_of_io_int].src_bus_id = m->src_bus;
    x86_mp_conf.io_int[x86_mp_conf.num_of_io_int].src_bus_irq = m->src_bus_irq;
    x86_mp_conf.io_int[x86_mp_conf.num_of_io_int].dst_io_apic_id = m->dst_apic;
    x86_mp_conf.io_int[x86_mp_conf.num_of_io_int].dst_io_apic_irq = m->dst_irq;
    x86_mp_conf.num_of_io_int++;

    return sizeof(struct mp_config_int_src);
}

static inline prtos_s32_t mp_add_lint(void *entry) {
    x86_mp_conf.num_of_line_int++;
    return sizeof(struct mp_config_level_int_src);
}

static prtos_s32_t (*add_mp_item[])(void *entry) = {
    [MP_PROCESSOR] = mp_add_procceor,
    [MP_BUS] = mp_add_bus,
    [MP_IOAPIC] = mp_add_io_apic,
    [MP_INTSRC] = mp_add_int_src,
    [MP_LINTSRC] = mp_add_lint,
    [MP_MAX] = 0,
};

static inline void *mp_test_config_tab(struct mp_floating_pointer *mpf) {
    struct mp_config_table *mpc;
    prtos_s32_t std_conf;

    mpc = (void *)((prtos_address_t)mpf->phys_ptr);
    if ((std_conf = (mpf->feature1 & 0xFF))) {
        x86system_panic("Std. configuration %d found\n", std_conf);
    }
    if (!mpf->phys_ptr) {
        x86system_panic("Not std. conf. found and conf. tables missed\n");
    }
    if (mpc->signature != MPC_SIGNATURE) {
        x86system_panic("SMP mptable: bad signature [0x%x]\n", mpc->signature);
    }
    if (mpf_checksum((prtos_u8_t *)mpc, mpc->length)) {
        x86system_panic("SMP mptable: checksum error\n");
    }
    if ((mpc->spec != 0x01) && (mpc->spec != 0x04)) {
        x86system_panic("SMP mptable: bad table version (%d)\n", mpc->spec);
    }
    if (!mpc->lapic) {
        x86system_panic("SMP mptable: null local APIC address\n");
    }
    x86_mp_conf.imcr = (mpf->feature2 & ICMR_FLAG) ? 1 : 0;

    return mpc;
}

static void __VBOOT parse_mp(struct mp_floating_pointer *mpf) {
    struct mp_config_table *mpc;
    prtos_s32_t ret, count;
    prtos_u8_t *mpt;

    mpc = mp_test_config_tab(mpf);
    count = sizeof(struct mp_config_table);
    mpt = (prtos_u8_t *)mpc + count;
    while (count < mpc->length) {
        if (*mpt < MP_MAX) {
            if (add_mp_item[*mpt]) {
                ret = add_mp_item[*mpt](mpt);
                count += ret;
                mpt += ret;
            }
        } else {
            count = mpc->length;
        }
    }
}

void __VBOOT init_smp_mpspec(void) {
    struct mp_floating_pointer *mpf;

    mpf = mp_scan_config();
    if (!mpf) {
        x86system_panic("SMP mpspec: MP-table not found\n");
    }
    parse_mp(mpf);
}

#endif /*CONFIG_SMP_INTERFACE_MPSPEC*/
