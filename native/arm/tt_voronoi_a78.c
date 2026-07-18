/* ==========================================================================
 * tt_voronoi.c -- Voronoi containment check, Cortex-M reference.
 *
 * For a broadcast domain of m ECUs (m = 6..14 in the ROAD topology), a
 * Kirkpatrick point-location structure is strictly worse than a linear
 * nearest-centroid scan: O(log m) with a pointer-chasing tree descent and
 * unpredictable branches costs more, on an in-order M-profile core, than
 * O(m) with m <= 16 fully-predictable straight-line iterations.  Nearest
 * centroid IS Voronoi membership, so nothing is given up.
 *
 * Coordinates are Q16.16 fixed point; no FPU is assumed (M0+ and M3 have none).
 * ========================================================================== */

#include <stdint.h>

#define TT_MAX_ECU 16          /* domain size, padded to a constant */
/* EMA alpha = 7/8: c <- c - (c>>3) + (q>>3). Pure shifts/adds, no divide,
 * no 64-bit helper call. This is the deployed configuration. */

typedef struct { int32_t x, y; } pt_q16;   /* Q16.16 */

/* squared Euclidean distance, Q16.16 inputs -> Q32 result.
 * Deltas are bounded by the unit square, so the 32x32->64 product cannot
 * overflow; on M0+ we use the 32x32->32 multiplier after a shift. */
static inline uint32_t dist2(pt_q16 a, pt_q16 b)
{
    int32_t dx = (a.x - b.x) >> 8;      /* Q8.8 */
    int32_t dy = (a.y - b.y) >> 8;
    return (uint32_t)(dx * dx + dy * dy);
}

/* Returns 1 if the running centroid c is contained in V(C_i) AND inside the
 * distance gate theta; 0 if it is an anomaly.  Branch-free in the scan:
 * the argmin is computed with a conditional-select idiom, not a branch. */
int tt_voronoi_check(pt_q16 c,
                     const pt_q16 *C, int m,     /* domain centroids */
                     int i,                       /* claimed sender index */
                     uint32_t theta2)             /* theta^2, Q16 */
{
    uint32_t best = 0xFFFFFFFFu;
    int besti = 0;

#pragma GCC unroll 16
    for (int k = 0; k < TT_MAX_ECU; k++) {
        uint32_t d = (k < m) ? dist2(c, C[k]) : 0xFFFFFFFFu;
        /* conditional select, no branch */
        int lt = (d < best);
        best  = lt ? d : best;
        besti = lt ? k : besti;
    }

    uint32_t di = dist2(c, C[i]);
    return (besti == i) && (di <= theta2);
}

/* EMA update of the running centroid: c <- 0.9*c + 0.1*q */
void tt_ema_update(pt_q16 *c, pt_q16 q)
{
    c->x += (q.x - c->x) >> 3;   /* c + (q-c)/8  ==  (7c + q)/8 */
    c->y += (q.y - c->y) >> 3;
}
