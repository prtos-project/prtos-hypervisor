/*
 * FILE: acpi.c
 *
 * Advanced Configuration and Power Interface
 *
 * www.prtos.org
 */
#include <arch/acpi.h>
#include <arch/paging.h>
#include <assert.h>
#include <boot.h>
#include <sched.h>
#include <smp.h>

#ifdef CONFIG_SMP_INTERFACE_ACPI

static struct acpi_rsdp *acpi_rsdp;

static inline prtos_u32_t acpi_create_tmp_mapping(prtos_address_t addr) {
    prtos_u32_t *page_table;
    prtos_u32_t page, entry;

    entry = 0;
    page_table = (prtos_u32_t *)_PHYS2VIRT(save_cr3());
    page = addr & LPAGE_MASK;
    if (page < CONFIG_PRTOS_OFFSET) {
        entry = page_table[VADDR_TO_PDE_INDEX(page)];
        page_table[VADDR_TO_PDE_INDEX(page)] = page | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW | _PG_ARCH_GLOBAL;
    }

    return entry;
}

static inline void acpi_clear_tmp_mapping(prtos_address_t addr) {
    prtos_u32_t *page_table;
    prtos_u32_t page, entry;

    entry = 0;
    page_table = (prtos_u32_t *)_PHYS2VIRT(save_cr3());
    page = addr & LPAGE_MASK;
    if (page < CONFIG_PRTOS_OFFSET) {
        page_table[VADDR_TO_PDE_INDEX(page)] = 0;
    }
}

static inline prtos_s32_t check_signature(const char *str, const char *sig, prtos_s32_t len) {
    prtos_s32_t e;

    for (e = 0; e < len; ++e) {
        if (*str != *sig) {
            return 0;
        }
        ++str;
        ++sig;
    }

    return 1;
}

prtos_address_t __VBOOT acpi_scan(const char *signature, prtos_address_t ebdaStart, prtos_address_t ebdaEnd) {
    prtos_address_t addr;

    for (addr = ebdaStart; addr < ebdaEnd; addr += 16) {
        if (check_signature((char *)addr, signature, 8)) {
            return addr;
        }
    }

    return 0;
}

void acpi_madt_lapic_handle(struct acpi_madt_ics *ics) {
    static prtos_s32_t bsp = 1;

    if (ics->lapic.flags & CPU_ENABLED) {
        if (x86_mp_conf.num_of_cpu >= CONFIG_NO_CPUS) {
            x86system_panic("Only supported %d cpus\n", CONFIG_NO_CPUS);
        }
        if (bsp) {
            x86_mp_conf.cpu[x86_mp_conf.num_of_cpu].bsp = 1;
            bsp = 0;
        }
        x86_mp_conf.cpu[x86_mp_conf.num_of_cpu].enabled = 1;
        x86_mp_conf.cpu[x86_mp_conf.num_of_cpu].id = ics->lapic.apic_id;
        x86_mp_conf.num_of_cpu++;
    }
}

void acpi_madt_io_apic_handle(struct acpi_madt_ics *ics) {
    prtos_s32_t e;

    x86_mp_conf.io_apic[x86_mp_conf.num_of_io_apic].id = ics->io_apic.id;
    x86_mp_conf.io_apic[x86_mp_conf.num_of_io_apic].base_addr = ics->io_apic.ioapic_addr;
    for (e = 0; e < 24; ++e) {
        x86_mp_conf.io_int[e + x86_mp_conf.num_of_io_int].dst_io_apic_id = ics->io_apic.id;
        x86_mp_conf.io_int[e + x86_mp_conf.num_of_io_int].dst_io_apic_irq = e;
    }
    x86_mp_conf.num_of_io_int += 24;
    x86_mp_conf.num_of_io_apic++;
}

void acpi_madt_irq_src_handle(struct acpi_madt_ics *ics) {
    if (ics->irq_src.flags) {
        x86_mp_conf.io_int[ics->irq_src.gs_irq].irq_over = 1;
        switch (ics->irq_src.flags & 0x3) {
            case 1:
                x86_mp_conf.io_int[ics->irq_src.gs_irq].polarity = 0;
                break;
            case 3:
                x86_mp_conf.io_int[ics->irq_src.gs_irq].polarity = 1;
                break;
        }
        switch ((ics->irq_src.flags >> 2) & 0x3) {
            case 1:
                x86_mp_conf.io_int[ics->irq_src.gs_irq].trigger_mode = 0;
                break;
            case 3:
                x86_mp_conf.io_int[ics->irq_src.gs_irq].trigger_mode = 1;
                break;
        }
    }
}

static void (*acpi_madt_handle[])(struct acpi_madt_ics *) = {
    [ACPI_MADT_LAPIC] = acpi_madt_lapic_handle,
    [ACPI_MADT_IOAPIC] = acpi_madt_io_apic_handle,
    [ACPI_MADT_IRQ_SRC] = acpi_madt_irq_src_handle,
    [ACPI_MADT_MAX] = 0,
};

static void __VBOOT acpi_parse_madt_table(struct acpi_madt *madt) {
    prtos_u32_t madtEnd, entry;
    struct acpi_madt_ics *ics;

    madtEnd = (prtos_u32_t)madt + madt->header.length;
    entry = (prtos_u32_t)madt + sizeof(struct acpi_madt);
    while (entry < madtEnd) {
        ics = (struct acpi_madt_ics *)entry;
        if (ics->type < ACPI_MADT_MAX) {
            if (acpi_madt_handle[ics->type]) {
                acpi_madt_handle[ics->type](ics);
            }
        }
        entry += ics->length;
    }
}

static void __VBOOT acpi_parse_mp(struct acpi_rsdp *rsdp) {
    struct acpi_rsdt *rsdt;
    struct acpi_header *header;
    prtos_s32_t e, entries;

    acpi_create_tmp_mapping(rsdp->rsdt_addr);
    rsdt = (struct acpi_rsdt *)rsdp->rsdt_addr;
    header = &rsdt->header;
    entries = (header->length - sizeof(struct acpi_header)) >> 2;
    for (e = 0; e < entries; ++e) {
        header = (struct acpi_header *)rsdt->entry[e];
        if (check_signature(header->signature, "APIC", 4)) {
            acpi_parse_madt_table((struct acpi_madt *)header);
        }
    }
    acpi_clear_tmp_mapping(rsdp->rsdt_addr);
}

void __VBOOT init_smp_acpi(void) {
    acpi_rsdp = (struct acpi_rsdp *)acpi_scan("RSD PTR ", (0xe0000), (0xfffff));
    if (!acpi_rsdp) {
        x86system_panic("ACPI: No RSDP found\n");
    }
    acpi_parse_mp(acpi_rsdp);
}
#endif /*CONFIG_SMP_INTERFACE_ACPI*/
