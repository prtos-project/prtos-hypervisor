/*
 * FILE: compress.h
 *
 * compression functions
 *
 * www.prtos.org
 */

#ifndef _LIBXEF_COMPRESS_H_
#define _LIBXEF_COMPRESS_H_

#define COMPRESS_BAD_MAGIC -1
#define COMPRESS_BUFFER_OVERRUN -2
#define COMPRESS_ERROR_LZ -3
#define COMPRESS_READ_ERROR -4
#define COMPRESS_WRITE_ERROR -5

typedef prtos_s32_t (*c_func_t)(void *buffer, prtos_u_size_t size, void *data);

extern prtos_s32_t compress(prtos_u32_t in_size, prtos_u32_t outSize, c_func_t read, void *read_data, c_func_t write, void *write_data,
                            void (*seekW)(prtos_s_size_t offset, void *write_data));
extern prtos_s32_t uncompress(prtos_u32_t in_size, prtos_u32_t outSize, c_func_t read, void *read_data, c_func_t write, void *write_data);

#endif
