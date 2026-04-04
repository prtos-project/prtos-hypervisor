/*
 * FILE: common.h
 *
 * PRTOS Partition Manager - Common utilities
 *
 */

#ifndef _PRTOS_MANAGER_COMMON_H_
#define _PRTOS_MANAGER_COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "prtos_hv.h"

#define NELEM(ary) (sizeof(ary)/sizeof(ary[0]))

int SplitLine(char *line, char *arg[], int nargs, int tokenize);
char *FindCommand(void *cmdfunc);
int GetCommandIndex(char *line, char *cmdlist[], int nelem);
int CheckPartition(int id, void *cmdfunc);
int prtosprintf(char const *fmt, ...);

extern int nopart;         /* number of partitions */

static inline const char* ErrorToStr(int error)
{
    if (error > 0)
        return "";

    switch (error) {
    default:                        return "UNKNOWN_ERROR";
    case PRTOS_OK:                  return "PRTOS_OK";
    case PRTOS_UNKNOWN_HYPERCALL:   return "PRTOS_UNKNOWN_HYPERCALL";
    case PRTOS_INVALID_PARAM:       return "PRTOS_INVALID_PARAM";
    case PRTOS_PERM_ERROR:          return "PRTOS_PERM_ERROR";
    case PRTOS_INVALID_CONFIG:      return "PRTOS_INVALID_CONFIG";
    case PRTOS_INVALID_MODE:        return "PRTOS_INVALID_MODE";
    case PRTOS_NO_ACTION:           return "PRTOS_NO_ACTION";
    case PRTOS_OP_NOT_ALLOWED:      return "PRTOS_OP_NOT_ALLOWED";
    }
}

static inline const char *StateToStr(int state)
{
    switch (state) {
    default:                        return "PRTOS_STATUS_UNKNOWN";
    case PRTOS_STATUS_IDLE:         return "PRTOS_STATUS_IDLE";
    case PRTOS_STATUS_READY:        return "PRTOS_STATUS_READY";
    case PRTOS_STATUS_SUSPENDED:    return "PRTOS_STATUS_SUSPENDED";
    case PRTOS_STATUS_HALTED:       return "PRTOS_STATUS_HALTED";
    }
}

#endif /* _PRTOS_MANAGER_COMMON_H_ */
