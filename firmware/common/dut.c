/* dut.c -- code under test. The tag/voronoi/ema bodies are byte-identical to
 * the implementations cycle-counted in the paper (arm/tt_tag.c, tt_voronoi.c).
 */
#include "dut.h"

#define TT_ROUNDS 59
#define TT_SCHED  0x0AD5A6B4D2E1F0ULL   /* provisioned Catalan schedule */
#define ROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))

#define RHO_OPEN(a,b,c,d)  ((a) ^ ROTL((uint32_t)((b)+(c)), 7))
#define RHO_CLOSE(a,b,c,d) ((a) ^ ROTL((uint32_t)((c)+(d)),11))
#define RND(j) do{ uint32_t t_=((TT_SCHED>>(j))&1ULL)?RHO_OPEN(s0,s1,s2,s3) \
                                                     :RHO_CLOSE(s0,s1,s2,s3); \
                   s0=s1;s1=s2;s2=s3;s3=t_; }while(0)

RAMFUNC uint16_t dut_tag(const uint32_t R[4], uint32_t w0, uint32_t w1,
                         uint32_t w2, uint32_t w3)
{
    uint32_t r0=R[0],r1=R[1],r2=R[2],r3=R[3];
    uint32_t s0=r0^w0, s1=r1^w1, s2=r2^w2, s3=r3^w3;
    RND(0);RND(1);RND(2);RND(3);RND(4);RND(5);RND(6);RND(7);RND(8);RND(9);
    RND(10);RND(11);RND(12);RND(13);RND(14);RND(15);RND(16);RND(17);RND(18);RND(19);
    RND(20);RND(21);RND(22);RND(23);RND(24);RND(25);RND(26);RND(27);RND(28);RND(29);
    RND(30);RND(31);RND(32);RND(33);RND(34);RND(35);RND(36);RND(37);RND(38);RND(39);
    RND(40);RND(41);RND(42);RND(43);RND(44);RND(45);RND(46);RND(47);RND(48);RND(49);
    RND(50);RND(51);RND(52);RND(53);RND(54);RND(55);RND(56);RND(57);RND(58);
    return (uint16_t)((s3 ^ r3) & 0xFFFFu);   /* low word of pack() is s3 */
}

#define TT_MAX_ECU 16
static inline uint32_t d2(pt_q16 a, pt_q16 b){
    int32_t dx=(a.x-b.x)>>8, dy=(a.y-b.y)>>8;
    return (uint32_t)(dx*dx+dy*dy);
}
RAMFUNC int dut_voronoi(pt_q16 c, const pt_q16 *C, int m, int i, uint32_t th2)
{
    uint32_t best=0xFFFFFFFFu; int bi=0;
    for(int k=0;k<TT_MAX_ECU;k++){
        uint32_t d=(k<m)?d2(c,C[k]):0xFFFFFFFFu;
        int lt=(d<best); best=lt?d:best; bi=lt?k:bi;
    }
    return (bi==i) && (d2(c,C[i])<=th2);
}

/* EMA smoothing factor.
 *   TT_ALPHA_SHIFT (default): alpha = 1 - 2^-s, i.e. 7/8 for s=3. Pure shifts
 *     and adds on every core, no 64-bit divide, no library call. This is the
 *     configuration specified for deployment in the paper (Sec. VII-D).
 *   TT_ALPHA_09: alpha = 0.9. Requires a divide by ten, which ARMv6-M resolves
 *     through __aeabi_ldivmod. Retained because the reported hardware
 *     measurements (605 cycles) were taken with this build. */
#ifndef TT_ALPHA_09
#define TT_ALPHA_SHIFT 3            /* alpha = 7/8 */
RAMFUNC void dut_ema(pt_q16 *c, pt_q16 q){
    c->x += (q.x - c->x) >> TT_ALPHA_SHIFT;
    c->y += (q.y - c->y) >> TT_ALPHA_SHIFT;
}
#else
#define ALPHA_NUM 9                 /* alpha = 0.9 */
RAMFUNC void dut_ema(pt_q16 *c, pt_q16 q){
    c->x=(int32_t)(((int64_t)ALPHA_NUM*c->x + q.x)/10);
    c->y=(int32_t)(((int64_t)ALPHA_NUM*c->y + q.y)/10);
}
#endif
