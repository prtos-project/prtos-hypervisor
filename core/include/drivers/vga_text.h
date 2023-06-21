/*
 * FILE: vga_text.h
 *
 * VGA text mode
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_VTEXT_H_
#define _PRTOS_ARCH_VTEXT_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <arch/io.h>
#include <arch/prtos_def.h>

#define TEXT_VGA_ADDRESS 0xb8000

#define R_MISC_OUTPUT 0x3cc
#define W_MISC_OUTPUT 0x3c2

#define CRTC_COM_REG 0x3d4
#define CRTC_DATA_REG 0x3d5
#define CURSOR_LOC_H 0x0e
#define CURSOR_LOC_L 0x0f
#define START_ADDR_H 0x0c
#define START_ADDR_L 0x0d

#define SEQ_COM_REG 0x3c4
#define SEQ_DATA_REG 0x3c5
#define CHAR_MAP_SELEC 0x3

#define vga_set_cursor_pos(cursorPos)                                  \
    do {                                                               \
        out_byte_port(CURSOR_LOC_H, CRTC_COM_REG);                             \
        out_byte_port((prtos_u8_t)(((cursorPos) >> 8)) & 0xff, CRTC_DATA_REG); \
        out_byte_port(CURSOR_LOC_L, CRTC_COM_REG);                             \
        out_byte_port((prtos_u8_t)((cursorPos)&0xff), CRTC_DATA_REG);          \
    } while (0)

#define VgaGetCursorPos()                          \
    do {                                           \
        prtos_u8_t tmp_H = 0, tmp_L = 0;           \
        prtos_u16_t tmp;                           \
        out_byte_port(CURSOR_LOC_H, CRTC_COM_REG);         \
        tmp_H = in_byte_port(CRTC_DATA_REG);               \
        out_byte_port(CURSOR_LOC_L, CRTC_COM_REG);         \
        tmp_L = in_byte_port(CRTC_DATA_REG);               \
        tmp = (prtos_u16_t)((tmp_H << 8) | tmp_L); \
        tmp;                                       \
    } while (0)

#define VgaSetStartAddr(start_addr)                                     \
    do {                                                                \
        out_byte_port(START_ADDR_H, CRTC_COM_REG);                              \
        out_byte_port((prtos_u8_t)(((start_addr) >> 8)) & 0xff, CRTC_DATA_REG); \
        out_byte_port(START_ADDR_L, CRTC_COM_REG);                              \
        out_byte_port((prtos_u8_t)((start_addr)&0xff), CRTC_DATA_REG);          \
    } while (0)

#define VgaGetStartAddr()                          \
    do {                                           \
        prtos_u8_t tmp_H = 0, tmp_L = 0;           \
        prtos_u16_t tmp;                           \
        out_byte_port(START_ADDR_H, CRTC_COM_REG);         \
        tmp_H = in_byte_port(CRTC_DATA_REG);               \
        out_byte_port(START_ADDR_L, CRTC_COM_REG);         \
        tmp_L = in_byte_port(CRTC_DATA_REG);               \
        tmp = (prtos_u16_t)((tmp_H << 8) | tmp_L); \
        tmp;                                       \
    } while (0)

#define VgaSetCharMapSelect()                 \
    do {                                      \
        prtos_u8_t tmp = 0;                   \
        out_byte_port(CHAR_MAP_SELEC, SEQ_COM_REG);   \
        tmp = (prtos_u8_t)in_byte_port(SEQ_DATA_REG); \
        tmp;                                  \
    } while (0)

#endif  // _ARCH_VTEXT_H_
