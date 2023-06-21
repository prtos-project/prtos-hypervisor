/*
 * FILE: io.h
 *
 * Port's related functions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_IO_H_
#define _PRTOS_ARCH_IO_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#define io_delay() __asm__ __volatile__("pushl %eax; inb $0x80,%al; inb $0x80,%al; popl %eax")

#define out_byte(val, port) __asm__ __volatile__("outb %0, %%dx\n\t" ::"a"((prtos_u8_t)(val)), "d"((prtos_u16_t)(port)))

#define out_word(val, port) __asm__ __volatile__("outw %0, %%dx\n\t" ::"a"((prtos_u16_t)(val)), "d"((prtos_u16_t)(port)))

#define out_line(val, port) __asm__ __volatile__("outl %0, %%dx\n\t" ::"a"((prtos_u32_t)(val)), "d"((prtos_u16_t)(port)))

#define out_byte_port(val, port) \
    ({                           \
        out_byte(val, port);     \
        io_delay();              \
        io_delay();              \
        io_delay();              \
    })

#define in_byte(port)                                                                           \
    ({                                                                                          \
        prtos_u8_t __inb_port;                                                                  \
        __asm__ __volatile__("inb %%dx, %0\n\t" : "=a"(__inb_port) : "d"((prtos_u16_t)(port))); \
        __inb_port;                                                                             \
    })

#define in_word(port)                                                                           \
    ({                                                                                          \
        prtos_u16_t __inw_port;                                                                 \
        __asm__ __volatile__("inw %%dx, %0\n\t" : "=a"(__inw_port) : "d"((prtos_u16_t)(port))); \
        __inw_port;                                                                             \
    })

#define in_line(port)                                                                           \
    ({                                                                                          \
        prtos_u32_t __inl_port;                                                                 \
        __asm__ __volatile__("inl %%dx, %0\n\t" : "=a"(__inl_port) : "d"((prtos_u16_t)(port))); \
        __inl_port;                                                                             \
    })

#define in_byte_port(port)          \
    ({                              \
        prtos_u8_t __inb_port;      \
        __inb_port = in_byte(port); \
        io_delay();                 \
        io_delay();                 \
        io_delay();                 \
        __inb_port;                 \
    })

#endif
