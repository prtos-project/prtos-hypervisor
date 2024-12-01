/*
 * FILE: serial.c
 *
 * Generic code to access the serial device on x86 platform
 *
 * www.prtos.org
 */

#ifdef CONFIG_OUTPUT_ENABLED

#define SPORT0 0x3F8 /*COM 1*/
#define SPORT1 0x2F8 /*COM 2*/
#define SPORT2 0x3E8 /*COM 3*/
#define SPORT3 0x2E8 /*COM 4*/

#define DEFAULT_PORT SPORT0

#define out_byte(val, port) __asm__ __volatile__("outb %0, %%dx\n\t" ::"a"((prtos_u8_t)(val)), "d"((prtos_u16_t)(port)))
#define in_byte(port)                                                                               \
    ({                                                                                              \
        prtos_u8_t __in_byte_port;                                                                  \
        __asm__ __volatile__("inb %%dx, %0\n\t" : "=a"(__in_byte_port) : "d"((prtos_u16_t)(port))); \
        __in_byte_port;                                                                             \
    })

void init_output(void) {
    // Configures COM1 for 115200 baud, 8-N-1 data format (8 bits, no parity, 1 stop bit).
    out_byte(0x00, DEFAULT_PORT + 1);
    out_byte(0x80, DEFAULT_PORT + 3);
    out_byte(0x01, DEFAULT_PORT + 0);
    out_byte(0x00, DEFAULT_PORT + 1);
    out_byte(0x03, DEFAULT_PORT + 3);
    out_byte(0xC7, DEFAULT_PORT + 2);
    out_byte(0x0B, DEFAULT_PORT + 4);
}

void xputchar(int c) {
    while (!(in_byte(DEFAULT_PORT + 5) & 0x20)) continue;
    out_byte(c, DEFAULT_PORT);
    if (c == '\n') {
        while (!(in_byte(DEFAULT_PORT + 5) & 0x20)) continue;
        out_byte('\r', DEFAULT_PORT);
    }
}

#else
void init_output(void) {}
void xputchar(int c) {}
#endif
