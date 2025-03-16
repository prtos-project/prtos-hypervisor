//#include "layout.h"

#ifdef CONFIG_OUTPUT_ENABLED

void init_output(void) {
    pl011_init();
}
void xputchar(int c) {
    pl011_putc(c);
}

#else
void init_output(void) {}
void xputchar(int c) {}
#endif