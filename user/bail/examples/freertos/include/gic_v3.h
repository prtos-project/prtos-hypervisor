/*
 * GICv3 Virtual CPU Interface (System Register Access)
 *
 * For FreeRTOS running as a PRTOS hw-virt partition at EL1,
 * GIC access uses ICC_* system registers (routed through the
 * GICv3 virtual CPU interface by ICH_HCR_EL2.En=1).
 */
#if !defined(_GIC_V3_H)
#define _GIC_V3_H

#if !defined(_BOARD_H)
#error "Include board.h before this header file."
#endif

#include "exception.h"

typedef int32_t irq_no;

/*
 * ICC system register access via inline assembly.
 * Encodings: ICC_IAR1_EL1  = S3_0_C12_C12_0
 *            ICC_EOIR1_EL1 = S3_0_C12_C12_1
 *            ICC_PMR_EL1   = S3_0_C4_C6_0
 *            ICC_BPR1_EL1  = S3_0_C12_C12_3
 *            ICC_RPR_EL1   = S3_0_C12_C11_3
 *            ICC_SRE_EL1   = S3_0_C12_C12_5
 *            ICC_IGRPEN1_EL1 = S3_0_C12_C12_7
 *            ICC_CTLR_EL1  = S3_0_C12_C12_4
 */

static inline uint32_t icc_read_iar1(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C12_C12_0" : "=r"(val));
	return (uint32_t)val;
}

static inline void icc_write_eoir1(uint32_t irq) {
	uint64_t val = irq;
	__asm volatile("msr S3_0_C12_C12_1, %0" :: "r"(val));
}

static inline uint32_t icc_read_pmr(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C4_C6_0" : "=r"(val));
	return (uint32_t)val;
}

static inline void icc_write_pmr(uint32_t prio) {
	uint64_t val = prio;
	__asm volatile("msr S3_0_C4_C6_0, %0" :: "r"(val));
}

static inline uint32_t icc_read_bpr1(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C12_C12_3" : "=r"(val));
	return (uint32_t)val;
}

static inline void icc_write_bpr1(uint32_t bpr) {
	uint64_t val = bpr;
	__asm volatile("msr S3_0_C12_C12_3, %0" :: "r"(val));
}

static inline uint32_t icc_read_rpr(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C12_C11_3" : "=r"(val));
	return (uint32_t)val;
}

static inline void icc_write_sre(uint32_t sre) {
	uint64_t val = sre;
	__asm volatile("msr S3_0_C12_C12_5, %0" :: "r"(val));
}

static inline uint32_t icc_read_sre(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C12_C12_5" : "=r"(val));
	return (uint32_t)val;
}

static inline void icc_write_igrpen1(uint32_t val) {
	uint64_t v = val;
	__asm volatile("msr S3_0_C12_C12_7, %0" :: "r"(v));
}

static inline void icc_write_ctlr(uint32_t val) {
	uint64_t v = val;
	__asm volatile("msr S3_0_C12_C12_4, %0" :: "r"(v));
}

/* Initialize virtual GIC interface (system registers only) */
void gic_v3_initialize(void);

#endif  /* _GIC_V3_H */
