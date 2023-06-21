/*
 * FILE: smp.c
 *
 * Generic routines smp
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <config.h>

extern struct prtos_image_hdr prtos_image_hdr;
extern prtos_address_t start[];

void setup_vcpus(void) {
    int i;
    if (prtos_get_vcpuid() == 0)
        for (i = 1; i < prtos_get_number_vcpus(); i++) prtos_reset_vcpu(i, prtos_image_hdr.page_table, (prtos_address_t)start, 0);
}
