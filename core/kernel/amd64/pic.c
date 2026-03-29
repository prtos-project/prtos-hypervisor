/*
 * FILE: pic.c
 *
 * The PC's PIC
 *
 * www.prtos.org
 */

#include <kdevice.h>
#include <irqs.h>
#include <spinlock.h>

#include <arch/io.h>
#include <arch/pic.h>
#include <arch/irqs.h>

RESERVE_IOPORTS(0x20, 2);
RESERVE_IOPORTS(0xa0, 2);

#define CTLR2_IRQ 0x2

RESERVE_HWIRQ(CTLR2_IRQ);

#define master_pic_iobase 0x20
#define slave_pic_iobase 0xa0
#define off_icw 0
#define off_ocw 1

#define master_icw master_pic_iobase + off_icw
#define master_ocw master_pic_iobase + off_ocw
#define slave_icw slave_pic_iobase + off_icw
#define slave_ocw slave_pic_iobase + off_ocw

#define LAST_IRQ_IN_MASTER 7

#define ICW_TEMPLATE 0x10
#define EDGE_TRIGGER 0x00
#define ADDR_INTRVL8 0x00
#define CASCADE_MODE 0x00
#define ICW4_NEEDED 0x01

#define SLAVE_ON_IR2 0x04
#define I_AM_SLAVE_2 0x02

#define SNF_MODE_DIS 0x00
#define NONBUFD_MODE 0x00
#define AUTO_EOI_MOD 0x02
#define NRML_EOI_MOD 0x00
#define I8086_EMM_MOD 0x01

#define PICM_ICW1 (ICW_TEMPLATE | EDGE_TRIGGER | ADDR_INTRVL8 | CASCADE_MODE | ICW4_NEEDED)
#define PICM_ICW3 SLAVE_ON_IR2
#define PICM_ICW4 (SNF_MODE_DIS | NONBUFD_MODE | NRML_EOI_MOD | I8086_EMM_MOD)

#define PICS_ICW1 (ICW_TEMPLATE | EDGE_TRIGGER | ADDR_INTRVL8 | CASCADE_MODE | ICW4_NEEDED)
#define PICS_ICW3 I_AM_SLAVE_2
#define PICS_ICW4 (SNF_MODE_DIS | NONBUFD_MODE | NRML_EOI_MOD | I8086_EMM_MOD)

#define NON_SPEC_EOI 0x20

static prtos_u8_t pic_master_mask = 0xff, pic_slave_mask = 0xff;

static void pic_enable_irq(prtos_u32_t irq) {
    if (irq <= LAST_IRQ_IN_MASTER) {
        pic_master_mask &= ~(1 << irq);
        out_byte(pic_master_mask, master_ocw);
    } else {
        pic_master_mask &= ~(1 << CTLR2_IRQ);
        pic_slave_mask &= ~(1 << (irq - LAST_IRQ_IN_MASTER - 1));
        out_byte(pic_master_mask, master_ocw);
        out_byte(pic_slave_mask, slave_ocw);
    }
    x86_hw_irqs_mask[GET_CPU_ID()] &= ~(1 << irq);
}

#ifndef CONFIG_APIC
void hw_irq_set_mask(prtos_s32_t e, prtos_u32_t mask) {
    pic_master_mask = mask & 0xff;
    pic_slave_mask = (mask >> 8) & 0xff;
    if (pic_slave_mask != 0xff) pic_master_mask &= ~(1 << CTLR2_IRQ);
    out_byte(pic_master_mask, master_ocw);
    out_byte(pic_slave_mask, slave_ocw);
}
#endif

static void pic_disable_irq(prtos_u32_t irq) {
    if (irq <= LAST_IRQ_IN_MASTER) {
        pic_master_mask |= (1 << irq);
        out_byte(pic_master_mask, master_ocw);
    } else {
        pic_slave_mask |= (1 << (irq - LAST_IRQ_IN_MASTER - 1));
        if (!pic_slave_mask) {
            pic_master_mask |= (1 << CTLR2_IRQ);
            out_byte(pic_master_mask, master_ocw);
        }
        out_byte(pic_slave_mask, slave_ocw);
    }
    x86_hw_irqs_mask[GET_CPU_ID()] |= (1 << irq);
}

static void pic_mask_and_ack_irq(prtos_u32_t irq) {
    if (irq <= LAST_IRQ_IN_MASTER) {
        pic_master_mask |= (1 << irq);
        out_byte_port(pic_master_mask, master_ocw);
        out_byte_port(NON_SPEC_EOI, master_icw);
    } else {
        pic_slave_mask |= (1 << (irq - LAST_IRQ_IN_MASTER - 1));
        if (!pic_slave_mask) {
            pic_master_mask |= (1 << CTLR2_IRQ);
            out_byte_port(pic_master_mask, master_ocw);
        }
        out_byte_port(pic_slave_mask, slave_ocw);
        out_byte_port(NON_SPEC_EOI, slave_icw);
        out_byte_port(NON_SPEC_EOI, master_icw);
    }
    x86_hw_irqs_mask[GET_CPU_ID()] |= (1 << irq);
}

void init_pic(prtos_u8_t master_base, prtos_u8_t slave_base) {
    prtos_s32_t irq;

    // initialise the master
    out_byte_port(PICM_ICW1, master_icw);
    out_byte_port(master_base, master_ocw);
    out_byte_port(PICM_ICW3, master_ocw);
    out_byte_port(PICM_ICW4, master_ocw);

    // initialise the slave
    out_byte_port(PICS_ICW1, slave_icw);
    out_byte_port(slave_base, slave_ocw);
    out_byte_port(PICS_ICW3, slave_ocw);
    out_byte_port(PICS_ICW4, slave_ocw);

    // ack any bogus intrs
    out_byte_port(NON_SPEC_EOI, master_icw);
    out_byte_port(NON_SPEC_EOI, slave_icw);

    // disable all the PIC's IRQ lines
    out_byte_port(pic_master_mask, master_ocw);
    out_byte_port(pic_slave_mask, slave_ocw);

    for (irq = 0; irq < PIC_IRQS; irq++) {
        hw_irq_ctrl[irq] = (hw_irq_ctrl_t){
            .enable = pic_enable_irq,
            .disable = pic_disable_irq,
            .ack = pic_mask_and_ack_irq,
            .end = pic_enable_irq,
        };
    }
}
