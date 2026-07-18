/* ==========================================================================
 * tt_tag.c -- Triangulating Trust tag function, Cortex-M reference.
 *
 * The Catalan schedule K is FIXED at provisioning, so TT_SCHED is a compile-
 * time constant.  The round sequence therefore unrolls into straight-line
 * code with NO branches, NO memory traffic inside the loop, and NO
 * data-dependent timing.  This is what makes exact static cycle counting
 * possible -- and, incidentally, makes the implementation constant-time by
 * construction (see the side-channel row of the security-boundary table).
 *
 * The four state words are kept in registers throughout; the "rotation" of
 * the generalised Feistel is realised by renaming, not by MOVs, so each round
 * costs exactly three data-processing instructions on M3/M4.
 * ========================================================================== */

#include <stdint.h>

#define TT_ROUNDS 59        /* |K| = 2n-1, n = 30 */

/* Schedule from the worked example (seed 0x0f1e...e1f0), LSB-first,
 * '(' = 1, ')' = 0.  Replaced at provisioning. */
#define TT_SCHED 0x0AD5A6B4D2E1F0ULL

#define ROTL(x, r) (((x) << (r)) | ((x) >> (32 - (r))))

/* rho_open  : s3' = s0 ^ ROTL(s1 + s2, 7),  then rename (s0,s1,s2,s3)<-(s1,s2,s3,s3')
 * rho_close : s3' = s0 ^ ROTL(s2 + s3, 11), then rename
 * The rename is free: we just permute the C variable names in the macro. */
#define RHO_OPEN(a, b, c, d)   ((a) ^ ROTL((uint32_t)((b) + (c)), 7))
#define RHO_CLOSE(a, b, c, d)  ((a) ^ ROTL((uint32_t)((c) + (d)), 11))

/* One round, chosen at COMPILE TIME by bit j of the constant schedule.
 * The ternary is resolved by the preprocessor/constant folder, so no branch
 * survives into the emitted code. */
#define RND(j)                                                        \
    do {                                                              \
        uint32_t t_ = ((TT_SCHED >> (j)) & 1ULL)                      \
                        ? RHO_OPEN (s0, s1, s2, s3)                   \
                        : RHO_CLOSE(s0, s1, s2, s3);                  \
        s0 = s1; s1 = s2; s2 = s3; s3 = t_;                           \
    } while (0)

uint16_t tt_tag_unrolled(const uint32_t *Rk,
                         uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    /* absorb: S <- R xor (qx||qy , ID , nc , D) */
    uint32_t r0 = Rk[0], r1 = Rk[1], r2 = Rk[2], r3 = Rk[3];
    uint32_t s0 = r0 ^ w0;
    uint32_t s1 = r1 ^ w1;
    uint32_t s2 = r2 ^ w2;
    uint32_t s3 = r3 ^ w3;

    RND(0);  RND(1);  RND(2);  RND(3);  RND(4);  RND(5);  RND(6);  RND(7);
    RND(8);  RND(9);  RND(10); RND(11); RND(12); RND(13); RND(14); RND(15);
    RND(16); RND(17); RND(18); RND(19); RND(20); RND(21); RND(22); RND(23);
    RND(24); RND(25); RND(26); RND(27); RND(28); RND(29); RND(30); RND(31);
    RND(32); RND(33); RND(34); RND(35); RND(36); RND(37); RND(38); RND(39);
    RND(40); RND(41); RND(42); RND(43); RND(44); RND(45); RND(46); RND(47);
    RND(48); RND(49); RND(50); RND(51); RND(52); RND(53); RND(54); RND(55);
    RND(56); RND(57); RND(58);

    /* squeeze: trunc_16(S xor R) */
    return (uint16_t)((s0 ^ r0) & 0xFFFFu);
}
