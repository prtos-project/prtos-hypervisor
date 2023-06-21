/*
 * FILE: pc_vga.c
 *
 * X86 PC screen driver
 *
 * www.prtos.org
 */
#ifdef CONFIG_DEV_VGA
#include <boot.h>
#include <rsvmem.h>
#include <kdevice.h>
#include <spinlock.h>
#include <sched.h>
#include <stdc.h>
#include <virtmm.h>
#include <vmmap.h>
#include <arch/physmm.h>
#include <drivers/vga_text.h>

#define COLUMNS 80
#define LINES 25
#define ATTRIBUTE 7

#define BUFFER_SIZE (COLUMNS * LINES * 2)

static const kdevice_t text_vga;
static const kdevice_t *get_text_vga(prtos_u32_t sub_id) {
    switch (sub_id) {
        case 0:
            return &text_vga;
            break;
    }

    return 0;
}

static prtos_u8_t *buffer;
static prtos_s32_t x_pos, y_pos;

static inline void clear_text_vga(void) {
    prtos_s32_t pos;

    x_pos = y_pos = 0;
    for (pos = 0; pos < COLUMNS * LINES; pos++) ((prtos_u16_t *)buffer)[pos] = (ATTRIBUTE << 8);

    vga_set_start_addr(0);
    vga_set_cursor_pos(0);
}

static inline prtos_u8_t *vga_map_textvga(void) {
    prtos_address_t addr, p;
    prtos_u32_t num_of_pages;

    num_of_pages = SIZE2PAGES(BUFFER_SIZE);
    addr = vmm_alloc(num_of_pages);
    for (p = 0; p < (num_of_pages * PAGE_SIZE); p += PAGE_SIZE) vm_map_page(TEXT_VGA_ADDRESS + p, addr + p, _PG_ATTR_PRESENT | _PG_ATTR_RW);

    return (prtos_u8_t *)addr;
}

prtos_s32_t init_text_vga(void) {
    get_kdev_table[PRTOS_DEV_VGA_ID] = get_text_vga;
    buffer = vga_map_textvga();

#ifndef CONFIG_EARLY_DEV_VGA
    clear_text_vga();
#endif

    return 0;
}

static inline void put_char_text_vga(prtos_s32_t c) {
    prtos_s32_t pos;

    if (c == '\t') {
        x_pos += 3;
        if (x_pos >= COLUMNS) goto newline;
        vga_set_cursor_pos((x_pos + y_pos * COLUMNS));
        return;
    }

    if (c == '\n' || c == '\r') {
    newline:
        x_pos = 0;
        y_pos++;
        if (y_pos == LINES) {
            memcpy((prtos_u8_t *)buffer, (prtos_u8_t *)&buffer[COLUMNS * 2], (LINES - 1) * COLUMNS * 2);
            for (pos = 0; pos < COLUMNS; pos++) ((prtos_u16_t *)buffer)[pos + (LINES - 1) * COLUMNS] = (ATTRIBUTE << 8);
            y_pos--;
        }

        vga_set_cursor_pos((x_pos + y_pos * COLUMNS));
        return;
    }

    buffer[(x_pos + y_pos * COLUMNS) * 2] = c & 0xFF;
    buffer[(x_pos + y_pos * COLUMNS) * 2 + 1] = ATTRIBUTE;

    x_pos++;
    if (x_pos >= COLUMNS) goto newline;

    vga_set_cursor_pos(x_pos + y_pos * COLUMNS);
}

static prtos_s32_t write_text_vga(const kdevice_t *kdev, prtos_u8_t *buffer, prtos_s32_t len) {
    prtos_s32_t e;
    for (e = 0; e < len; e++) put_char_text_vga(buffer[e]);

    return len;
}

static const kdevice_t text_vga = {
    .write = write_text_vga,
};

REGISTER_KDEV_SETUP(init_text_vga);

#ifdef CONFIG_EARLY_DEV_VGA
prtos_s32_t early_init_textvga(void) {
    buffer = (prtos_u8_t *)TEXT_VGA_ADDRESS;
    clear_text_vga();

    return 0;
}

void setup_early_output(void) {
    early_init_textvga();
}

void early_put_char(prtos_u8_t c) {
    put_char_text_vga(c);
}
#endif

#endif
