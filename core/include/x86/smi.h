/*
 * FILE: smi.h
 *
 * System Management Mode (SMM)
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_SMI_H_
#define _PRTOS_ARCH_SMI_H_

#define PM_BASE_B0 0x40
#define PM_BASE_B1 0x41
#define SMI_CTRL_ADDR 0x30
#define SMI_EN_BIT 0x01

#define ICH_VENDOR_INTEL 0x8086
#define NELEM(ary) (sizeof(ary) / sizeof(ary[0]))

static int ich_chipset_pciids[] = {
    0x2640, /* ICH6r0 */
    0x2641, /* ICH6r1 */
    0x2642, /* ICH6r2 */

    0x27b8, /* ICH7r8 */
    0x27b9, /* ICH7r9 */
    0x27da, /* ICH7 */
    0x2815, /* ICH8 */
    0x2916, /* ICH9 */
    0x3a16, /* ICH10 */
    0x27da, /* SMB */
};

static unsigned short get_smi_entry_addr(pciid_t id) {
    prtos_u8_t byte0, byte1;

    byte0 = pci_read_fonfig_byte(id, PM_BASE_B0);
    byte1 = pci_read_fonfig_byte(id, PM_BASE_B1);

    return SMI_CTRL_ADDR + (((byte1 << 1) | (byte0 >> 7)) << 7);  // bits 7-15
}

static int find_smi(void) {
    int ich;
    pciid_t id;
    pcidev_t dev;

    for (id = PCI_IDZERO; id != PCI_IDNULL; id = pci_next_dev(id)) {
        if (pci_id_info(id, &dev) == PCI_VENDOR_INVALID) continue;
        if (dev.vendor != ICH_VENDOR_INTEL) continue;
        for (ich = 0; ich < NELEM(ich_chipset_pciids); ich++) {
            if (dev.deviceid == ich_chipset_pciids[ich]) {
                return id;
            }
        }
    }
    return -1;
}

static inline int smi_disable(void) {
    pciid_t id;
    prtos_u32_t smi_entry_addr;

    id = find_smi();
    if (id <= 0) {
        return 0;
    }

    smi_entry_addr = get_smi_entry_addr(id);
    out_line(in_line(smi_entry_addr) & ~SMI_EN_BIT, smi_entry_addr);
    eprintf("SMI_EN bit disabled on ICH %x.%x\n", smi_entry_addr, in_line(smi_entry_addr));
    return 0;
}

#endif
