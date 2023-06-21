/*
 * FILE: pc_uart.c
 *
 * PC UART Driver
 *
 * www.prtos.org
 */

#include <kdevice.h>
#include <ktimer.h>
#if defined(CONFIG_x86) && defined(CONFIG_DEV_UART)
#include <irqs.h>
#include <arch/io.h>
#include <drivers/pc_uart.h>

RESERVE_HWIRQ(UART_IRQ0);
RESERVE_IOPORTS(DEFAULT_PORT, 5);

#define _UART_MAX_FREQ 115200
static void __init_uart(prtos_u32_t baudrate) {
    prtos_u16_t div;
    if (!baudrate) return;
    if (baudrate >= _UART_MAX_FREQ)
        div = 1;
    else
        div = _UART_MAX_FREQ / baudrate;

    out_byte(0x00, DEFAULT_PORT + 1);  // Disable all interrupts
    out_byte(0x80, DEFAULT_PORT + 3);  // Enable DLAB (set baud rate divisor)
    out_byte(div & 0xff, DEFAULT_PORT + 0);
    out_byte((div >> 8) & 0xff, DEFAULT_PORT + 1);
    out_byte(0x03, DEFAULT_PORT + 3);  // 8 bits, no parity, one stop bit
    out_byte(0xC7, DEFAULT_PORT + 2);  // Enable FIFO, clear them, with 14-byte threshold
    out_byte(0x0B, DEFAULT_PORT + 4);  // IRQs enabled, RTS/DSR set
}

static inline void put_char_uart(prtos_s32_t c) {
    while (!(in_byte(DEFAULT_PORT + 5) & 0x20)) continue;
    out_byte(c, DEFAULT_PORT);
}

static prtos_s32_t write_uart(const kdevice_t *kdev, prtos_u8_t *buffer, prtos_s32_t len) {
    prtos_s32_t e;
    for (e = 0; e < len; e++) put_char_uart(buffer[e]);

    return len;
}

static const kdevice_t uart_dev = {
    .write = write_uart,
};

static const kdevice_t *get_uart(prtos_u32_t sub_id) {
    switch (sub_id) {
        case 0:
            return &uart_dev;
            break;
    }

    return 0;
}

prtos_s32_t init_uart(void) {
    __init_uart(prtos_conf_table.device_table.uart[0].baud_rate);
    get_kdev_table[PRTOS_DEV_UART_ID] = get_uart;
    return 0;
}

REGISTER_KDEV_SETUP(init_uart);

#ifdef CONFIG_EARLY_DEV_UART
void setup_early_output(void) {
    init_uart();
}

void early_put_char(prtos_u8_t c) {
    put_char_uart(c);
}
#endif

#endif
