/*
 * FILE: prtos_manager_main.c
 *
 * PRTOS Partition Manager - Linux frontend (stdin/stdout)
 *
 * Usage:
 *   ./prtos_manager        # Interactive mode (uses vmcall for hypercalls)
 *   ./prtos_manager -d     # Dry-run mode (no hypervisor access)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "prtos_hv.h"
#include "prtos_manager.h"

enum {
    ModeNone,
    ModeDryRun
};

static int LinuxRead(char *line, int length)
{
    return read(0, line, length);
}

static int LinuxWrite(char *line, int length)
{
    return write(1, line, length);
}

static struct PrtosManagerDevice_t stdiodev = {
    .flags = !DEVICE_FLAG_COOKED,
    .init = 0,
    .read = LinuxRead,
    .write = LinuxWrite,
};

int main(int argc, char *argv[])
{
    int opt, mode;

    mode = ModeNone;
    while ((opt = getopt(argc, argv, "dh")) != -1) {
        switch (opt) {
        case 'd':
            mode = ModeDryRun;
            break;
        case 'h':
        default:
            printf("usage: prtos_manager [-d] [-h]\n");
            exit(1);
        }
    }

    if (mode == ModeNone) {
        if (prtos_hv_init() < 0) {
            fprintf(stderr, "[prtos_manager] Warning: Could not init hypervisor interface.\n");
            fprintf(stderr, "[prtos_manager] Running in limited mode (no hypercall access).\n");
        }
    }

    PrtosManager(&stdiodev);
    return 0;
}
