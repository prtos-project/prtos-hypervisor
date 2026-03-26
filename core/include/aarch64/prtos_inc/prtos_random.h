#ifndef __PRTOS_RANDOM_H__
#define __PRTOS_RANDOM_H__

unsigned int get_random(void);

/* The value keeps unchange once initialized for each booting */
extern unsigned int boot_random;

#endif /* __PRTOS_RANDOM_H__ */
