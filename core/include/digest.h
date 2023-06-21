/*
 * FILE: digest.h
 *
 * MD5 algorithm
 *
 * www.prtos.org
 */

#ifndef _PRTOS_MD5_H_
#define _PRTOS_MD5_H_

/* Data structure for MD5 (Message Digest) computation */
struct digest_ctx {
    prtos_u8_t in[64];
    prtos_u32_t buf[4];
    prtos_u32_t bits[2];
};

extern void digest_init(struct digest_ctx *md_context);
extern void digest_update(struct digest_ctx *ctx, const prtos_u8_t *buf, prtos_u32_t len);
extern void digest_final(prtos_u8_t digest[16], struct digest_ctx *ctx);

#endif
