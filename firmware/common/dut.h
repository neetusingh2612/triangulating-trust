/* ==========================================================================
 * dut.h / dut.c -- the code under test, wrapped for benchmarking.
 *
 * These wrap the exact tag / Voronoi / EMA implementations that were
 * cycle-counted in the paper (Section VII-B). Placing them in .ramfunc is
 * ESSENTIAL: measuring from flash adds wait states and inflates every number,
 * which is the single most common way to get a wrong latency figure.
 * ========================================================================== */
#ifndef DUT_H
#define DUT_H
#include <stdint.h>

/* RAMFUNC: put a function in RAM. GCC + a linker script that provides a
 * .ramfunc section (see linker note in README). If your toolchain differs,
 * define RAMFUNC to empty and instead set the whole .text to run from RAM. */
#ifndef RAMFUNC
#define RAMFUNC __attribute__((section(".ramfunc"), noinline))
#endif

/* one 16-bit tag over (ID, D, nonce), Catalan-scheduled ARX permutation */
RAMFUNC uint16_t dut_tag(const uint32_t R[4], uint32_t w0, uint32_t w1,
                         uint32_t w2, uint32_t w3);

/* Voronoi containment + distance-gate check over m<=16 domain centroids */
typedef struct { int32_t x, y; } pt_q16;
RAMFUNC int dut_voronoi(pt_q16 c, const pt_q16 *C, int m, int i, uint32_t th2);

/* EMA running-centroid update */
RAMFUNC void dut_ema(pt_q16 *c, pt_q16 q);

#endif
