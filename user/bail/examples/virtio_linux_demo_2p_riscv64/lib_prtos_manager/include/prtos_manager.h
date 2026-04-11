/*
 * FILE: prtos_manager.h
 *
 * PRTOS Partition Manager - Public API
 *
 */

#ifndef _PRTOS_MANAGER_H_
#define _PRTOS_MANAGER_H_

typedef struct PrtosManagerDevice_t PrtosManagerDevice_t;
struct PrtosManagerDevice_t {
#define DEVICE_FLAG_COOKED (1<<0)
    int flags;

    int (*init) (void);
    int (*read) (char *str, int len);
    int (*write) (char *str, int len);
};

int PrtosManager(PrtosManagerDevice_t *dev);

#endif /* _PRTOS_MANAGER_H_ */
