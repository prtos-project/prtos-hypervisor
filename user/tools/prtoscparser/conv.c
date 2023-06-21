/*
 * FILE: conv.c
 *
 * conversion helper functions
 *
 * www.prtos.org
 */

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "limits.h"
#include "common.h"
#include "parser.h"
#include "checks.h"
#include "prtos_conf.h"

#include <prtos_inc/audit.h>

prtos_u32_t to_version(char *s) {
    prtos_u32_t version, subversion, revision;

    sscanf(s, "%u.%u.%u", &version, &subversion, &revision);
    return PRTOSC_SET_VERSION(version, subversion, revision);
}

prtos_u32_t to_u32(char *s, int base) {
    prtos_u32_t u;

    u = strtoul(s, 0, base);
    return u;
}

int to_yes_no_true_false(char *s, int line) {
    if (!strcasecmp(s, "yes") || !strcasecmp(s, "true")) return 1;

    if (!strcasecmp(s, "no") || !strcasecmp(s, "false")) return 0;

    line_error(line, "expected yes|no|true|false");
    return 0;
}

prtos_u32_t to_freq(char *s) {
    double d;
    sscanf(s, "%lf", &d);
    if (strcasestr(s, "MHz")) {
        d *= 1000.0;
    } /*else if(strcasestr(b, "KHz")) {
        d*=1000.0;
        } */
    if (d >= UINT_MAX) error_printf("Frequency exceeds MAX_UINT");
    return (prtos_u32_t)d;
}

prtos_u32_t to_time(char *s) {
    double d;
    sscanf(s, "%lf", &d);
    if (strcasestr(s, "us")) {
    } else if (strcasestr(s, "ms")) {
        d *= 1000.0;
    } else if (strcasestr(s, "s")) {
        d *= 1000000.0;
    }
    if (d >= UINT_MAX) error_printf("Time exceeds MAX_UINT");
    return (prtos_u32_t)d;
}

prtos_u_size_t to_size(char *s) {
    double d;
    sscanf(s, "%lf", &d);
    if (strcasestr(s, "MB")) {
        d *= (1024.0 * 1024.0);
    } else if (strcasestr(s, "KB")) {
        d *= 1024.0;
    } /*else  if(strcasestr(s, "B")) {
        } */

    if (d >= UINT_MAX) error_printf("Size exceeds MAX_UINT");

    return (prtos_u_size_t)d;
}

prtos_u32_t to_comm_port_type(char *s, int line) {
    if (!strcmp(s, "queuing")) return PRTOS_QUEUING_PORT;

    if (!strcmp(s, "sampling")) return PRTOS_SAMPLING_PORT;

    line_error(line, "expected a valid communication port type");
    return 0;
}

struct attr2flags {
    char attr[32];
    prtos_u32_t flag;
};

#define NO_PARTITION_FLAGS 3
static struct attr2flags partition_flags_table[NO_PARTITION_FLAGS] = {
    [0] =
        {
            .attr = "system",
            .flag = PRTOS_PART_SYSTEM,
        },
    [1] =
        {
            .attr = "fp",
            .flag = PRTOS_PART_FP,
        },
    [2] =
        {
            .attr = "none",
            .flag = 0x0,
        },
};

#ifdef CONFIG_AUDIT_EVENTS
#define NO_BITMASK_TRACE_HYP 4
static struct attr2flags bitmask_trace_hyp_table[NO_BITMASK_TRACE_HYP] = {
    [0] =
        {
            .attr = "HYP_IRQS",
            .flag = TRACE_BM_IRQ_MODULE,
        },
    [1] =
        {
            .attr = "HYP_HCALLS",
            .flag = TRACE_BM_HCALLS_MODULE,
        },
    [2] =
        {
            .attr = "HYP_SCHED",
            .flag = TRACE_BM_SCHED_MODULE,
        },
    [3] =
        {
            .attr = "HYP_HM",
            .flag = TRACE_BM_HM_MODULE,
        },
};
#endif

#define NO_PHYMEM_AREA_FLAGS 11
static struct attr2flags phys_mem_area_flags_table[NO_PHYMEM_AREA_FLAGS] = {
    [0] =
        {
            .attr = "unmapped",
            .flag = PRTOS_MEM_AREA_UNMAPPED,
        },
    [1] =
        {
            .attr = "shared",
            .flag = PRTOS_MEM_AREA_SHARED,
        },
    [2] =
        {
            .attr = "read-only",
            .flag = PRTOS_MEM_AREA_READONLY,
        },
    [3] =
        {
            .attr = "uncacheable",
            .flag = PRTOS_MEM_AREA_UNCACHEABLE,
        },
    [4] =
        {
            .attr = "rom",
            .flag = PRTOS_MEM_AREA_ROM,
        },
    [5] =
        {
            .attr = "flag0",
            .flag = PRTOS_MEM_AREA_FLAG0,
        },
    [6] =
        {
            .attr = "flag1",
            .flag = PRTOS_MEM_AREA_FLAG1,
        },
    [7] =
        {
            .attr = "flag2",
            .flag = PRTOS_MEM_AREA_FLAG2,
        },
    [8] =
        {
            .attr = "flag3",
            .flag = PRTOS_MEM_AREA_FLAG3,
        },
    [9] =
        {
            .attr = "iommu",
            .flag = PRTOS_MEM_AREA_IOMMU,
        },
    [10] =
        {
            .attr = "none",
            .flag = 0x0,
        },
};

void process_id_list(char *s, void (*call_back)(int, char *), int line) {
    char *tmp, *tmp1;

    for (tmp = s, tmp1 = strstr(tmp, " "); *tmp;) {
        if (tmp1) *tmp1 = 0;

        call_back(line, tmp);

        if (tmp1)
            tmp = tmp1 + 1;
        else
            break;
        tmp1 = strstr(tmp, " ");
    }
}

static prtos_u32_t to_flags(char *s, struct attr2flags *attr2flags, int noElem, int line) {
    prtos_u32_t flags = 0, found = 0;
    char *tmp, *tmp1;
    int e;

    for (tmp = s, tmp1 = strstr(tmp, " "); *tmp;) {
        if (tmp1) *tmp1 = 0;
        for (e = 0; e < noElem; e++)
            if (!strcasecmp(tmp, attr2flags[e].attr)) {
                flags |= attr2flags[e].flag;
                found = 1;
            }
        if (!found) line_error(line, "expected valid flag (%s)\n", tmp);
        found = 0;

        if (tmp1)
            tmp = tmp1 + 1;
        else
            break;
        tmp1 = strstr(tmp, " ");
    }

    return flags;
}

prtos_u32_t to_partition_flags(char *s, int line) {
    return to_flags(s, partition_flags_table, NO_PARTITION_FLAGS, line);
}

prtos_u32_t to_bitmask_trace_hyp(char *s, int line) {
#ifdef CONFIG_AUDIT_EVENTS
    return to_flags(s, bitmask_trace_hyp_table, NO_BITMASK_TRACE_HYP, line);
#else
    return 0;
#endif
}

prtos_u32_t to_phys_mem_area_flags(char *s, int line) {
    return to_flags(s, phys_mem_area_flags_table, NO_PHYMEM_AREA_FLAGS, line);
}

prtos_u32_t to_comm_port_direction(char *s, int line) {
    if (!strcmp(s, "source")) return PRTOS_SOURCE_PORT;

    if (!strcmp(s, "destination")) return PRTOS_DESTINATION_PORT;

    line_error(line, "expected a valid communication port direction");
    return 0;
}

void to_hw_irq_lines(char *s, int line_number) {
    char *tmp, *tmp1;
    int line;

    for (tmp = s, tmp1 = strstr(tmp, " "); *tmp;) {
        if (tmp1) *tmp1 = 0;

        line = atoi(tmp);
        if (line >= CONFIG_NO_HWIRQS) line_error(line_number, "invalid hw interrupt line (%d)", line);
        if (prtos_conf.hpv.hw_irq_table[line].owner != PRTOS_IRQ_NO_OWNER)
            line_error(line_number, "hw interrupt line (%d) already assigned (line %d)", line, prtos_conf_line_number.hpv.hw_irq_table[line]);

        check_hw_irq(line, line_number);
        prtos_conf.hpv.hw_irq_table[line].owner = prtos_conf.num_of_partitions - 1;
        prtos_conf_line_number.hpv.hw_irq_table[line] = line_number;
        if (tmp1)
            tmp = tmp1 + 1;
        else
            break;
        tmp1 = strstr(tmp, " ");
    }
}
