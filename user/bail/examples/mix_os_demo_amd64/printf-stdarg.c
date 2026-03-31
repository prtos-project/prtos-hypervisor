/*
 * printf for PRTOS para-virt partition using BAIL's write_console.
 */
#include <stdarg.h>

/* BAIL provides printf already via write_console / prtos_write_console */
/* But we need to provide our own printf-stdarg since FreeRTOS example.c uses it */

extern int prtos_write_console(const char *buffer, int length);

static void putchar_prtos(char c)
{
	prtos_write_console(&c, 1);
}

#define putchar(c) putchar_prtos(c)
#define PAD_RIGHT 1
#define PAD_ZERO 2

static void printchar(char **str, int c)
{
	if (str) {
		**str = c;
		++(*str);
	}
	else (void)putchar((const char) c);
}

static int prints(char **out, const char *string, int width, int pad)
{
	register int pc = 0, padchar = ' ';
	if (width > 0) {
		register int len = 0;
		register const char *ptr;
		for (ptr = string; *ptr; ++ptr) ++len;
		if (len >= width) width = 0;
		else width -= len;
		if (pad & PAD_ZERO) padchar = '0';
	}
	if (!(pad & PAD_RIGHT)) {
		for ( ; width > 0; --width) { printchar(out, padchar); ++pc; }
	}
	for ( ; *string ; ++string) { printchar(out, *string); ++pc; }
	for ( ; width > 0; --width) { printchar(out, padchar); ++pc; }
	return pc;
}

#define PRINT_BUF_LEN 12

static int printi(char **out, int i, int b, int sg, int width, int pad, int letbase)
{
	char print_buf[PRINT_BUF_LEN];
	register char *s;
	register int t, neg = 0, pc = 0;
	register unsigned int u = i;
	if (i == 0) { print_buf[0] = '0'; print_buf[1] = '\0'; return prints(out, print_buf, width, pad); }
	if (sg && b == 10 && i < 0) { neg = 1; u = -i; }
	s = print_buf + PRINT_BUF_LEN-1;
	*s = '\0';
	while (u) { t = u % b; if( t >= 10 ) t += letbase - '0' - 10; *--s = t + '0'; u /= b; }
	if (neg) { if( width && (pad & PAD_ZERO) ) { printchar(out, '-'); ++pc; --width; } else { *--s = '-'; } }
	return pc + prints(out, s, width, pad);
}

static int print(char **out, const char *format, va_list args)
{
	register int width, pad;
	register int pc = 0;
	char scr[2];
	for (; *format != 0; ++format) {
		if (*format == '%') {
			++format;
			width = pad = 0;
			if (*format == '\0') break;
			if (*format == '%') goto out;
			if (*format == '-') { ++format; pad = PAD_RIGHT; }
			while (*format == '0') { ++format; pad |= PAD_ZERO; }
			for ( ; *format >= '0' && *format <= '9'; ++format) { width *= 10; width += *format - '0'; }
			if( *format == 's' ) { register char *s = (char *)va_arg(args, char *); pc += prints(out, s?s:"(null)", width, pad); continue; }
			if( *format == 'd' ) { pc += printi(out, va_arg(args, int), 10, 1, width, pad, 'a'); continue; }
			if( *format == 'x' ) { pc += printi(out, va_arg(args, int), 16, 0, width, pad, 'a'); continue; }
			if( *format == 'X' ) { pc += printi(out, va_arg(args, int), 16, 0, width, pad, 'A'); continue; }
			if( *format == 'u' ) { pc += printi(out, va_arg(args, int), 10, 0, width, pad, 'a'); continue; }
			if( *format == 'c' ) { scr[0] = (char)va_arg(args, int); scr[1] = '\0'; pc += prints(out, scr, width, pad); continue; }
		} else {
		out:
			printchar(out, *format);
			++pc;
		}
	}
	if (out) **out = '\0';
	va_end(args);
	return pc;
}

int printf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	return print(0, format, args);
}

int sprintf(char *out, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	return print(&out, format, args);
}

/* uart_* wrappers for FreeRTOS example.c compatibility */
void uart_init(void) { /* PRTOS console is always ready */ }

void uart_putc(const char c) { putchar_prtos(c); }

void uart_puthex(unsigned long n)
{
	const char *hexdigits = "0123456789ABCDEF";
	uart_putc('0'); uart_putc('x');
	for (int i = 60; i >= 0; i -= 4) {
		uart_putc(hexdigits[(n >> i) & 0xf]);
		if (i == 32) uart_putc(' ');
	}
}

void uart_puts(const char *s)
{
	for (int i = 0; s[i] != '\0'; i++)
		uart_putc((unsigned char)s[i]);
}
