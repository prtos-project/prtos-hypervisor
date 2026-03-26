#ifndef __PRTOS_HV_VERSION_H__
#define __PRTOS_HV_VERSION_H__

#include <prtos_types.h>
#include <prtos_elfstructs.h>

const char *prtos_compile_date(void);
const char *prtos_compile_time(void);
const char *prtos_compile_by(void);
const char *prtos_compile_domain(void);
const char *prtos_compile_host(void);
const char *prtos_compiler(void);
unsigned int prtos_major_version(void);
unsigned int prtos_minor_version(void);
const char *prtos_extra_version(void);
const char *prtos_changeset(void);
const char *prtos_banner(void);
const char *prtos_deny(void);
const char *prtos_build_info(void);
int prtos_build_id(const void **p, unsigned int *len);

#ifdef BUILD_ID
void prtos_build_init(void);
int prtos_build_id_check(const Elf_Note *n, unsigned int n_sz,
                       const void **p, unsigned int *len);
#else
static inline void prtos_build_init(void) {};
#endif

#endif /* __PRTOS_HV_VERSION_H__ */
