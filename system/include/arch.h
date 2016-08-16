#ifndef __ARCH_H_
#define __ARCH_H_

#define L1D_CACHELINE_SIZE (64)
#define __cacheline_aligned __attribute__((aligned(L1D_CACHELINE_SIZE)))

#define __cacheline_aligned2 __attribute__((aligned(2 * L1D_CACHELINE_SIZE)))

static inline void nop_pause(void) { __asm __volatile("pause"); }

#endif /* __ARCH_H_ */
