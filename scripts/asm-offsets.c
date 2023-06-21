/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#include <config.h>
#include <arch/arch_types.h>
#include <stdc.h>

#define offsetof(_t, _m) OFFSETOF(_t, _m)

#define DEFINE(sym, val, marker) asm volatile("\n-> " #sym " %0 " #val " " #marker : : "i"(val))

#define DEFINE2(sym, val, marker) asm volatile("\n8> " #sym " %0 " #val " " #marker : : "i"(val))

#define BLANK() asm volatile("\n->" : :)

#include _OFFS_FILE_

int main(void) {
    generate_offsets();
    BLANK();
    return 0;
}
