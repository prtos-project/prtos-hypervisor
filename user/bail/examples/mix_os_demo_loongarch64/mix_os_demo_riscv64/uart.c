/*
 * uart.c - PRTOS hypercall-based UART output for FreeRTOS in mixed-OS demo.
 *
 * In the mixed-OS config the physical UART is assigned to the Linux
 * partition, so FreeRTOS uses the PRTOS write_object hypercall to write
 * to the console through the hypervisor.
 */

#include "board.h"
#include "uart.h"

/* PRTOS hypercall: write_object (nr=12) */
#define WRITE_OBJECT_NR  12

/* Object descriptor: OBJ_CLASS_CONSOLE=1, partition_id=1, id=0
 * Format: [31:valid][30:24:class][23:20:vcpuid][19:10:partid][9:0:id]
 * = (1 << 24) | (0 << 20) | (1 << 10) | 0 = 0x01000400 */
#define CONSOLE_OBJ_DESC  0x01000400U

static inline long prtos_write_console(const void *buf, unsigned long size)
{
	register unsigned long a0 __asm__("a0") = (unsigned long)WRITE_OBJECT_NR;
	register unsigned long a1 __asm__("a1") = (unsigned long)CONSOLE_OBJ_DESC;
	register unsigned long a2 __asm__("a2") = (unsigned long)buf;
	register unsigned long a3 __asm__("a3") = (unsigned long)size;
	register unsigned long a4 __asm__("a4") = 0;
	__asm__ __volatile__("ecall"
		: "+r"(a0)
		: "r"(a1), "r"(a2), "r"(a3), "r"(a4)
		: "memory");
	return (long)a0;
}

void uart_init(void)
{
	/* No init needed for PRTOS console */
}

void uart_putc(char c)
{
	prtos_write_console(&c, 1);
}

void uart_puts(const char *s)
{
	/* Send in chunks of up to 128 bytes (kernel console limit) */
	while (*s) {
		const char *p = s;
		int len = 0;
		while (*p && len < 128) {
			p++;
			len++;
		}
		prtos_write_console(s, len);
		s += len;
	}
}

void uart_puthex(unsigned long n)
{
	const char *hex = "0123456789ABCDEF";
	char buf[18]; /* "0x" + 16 hex digits */
	int i;

	buf[0] = '0';
	buf[1] = 'x';
	for (i = 0; i < 16; i++)
		buf[2 + i] = hex[(n >> (60 - i * 4)) & 0xF];
	prtos_write_console(buf, 18);
}
