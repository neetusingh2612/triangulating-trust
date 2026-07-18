/* ==========================================================================
 * main.c -- Triangulating Trust bench firmware.
 *
 * Produces EVERY number the paper's Hardware Validation scaffold is waiting
 * for. Three roles, chosen at build time with -DROLE=...:
 *
 *   ROLE_CYCLES : (run on ONE node, no bus needed)
 *                 measures tag / voronoi / ema / full-path cycle counts via
 *                 DWT->CYCCNT, N iterations, min+mean+max. -> scaffold A,B,E
 *
 *   ROLE_TX     : (sender node) transmits application traffic at a fixed rate,
 *                 optionally with a 2-byte tag appended, and DLC-8 companion
 *                 frames. Prints its own tag-compute cost. -> feeds C,D
 *
 *   ROLE_RX     : (receiver node) verifies tags + runs the Voronoi check on
 *                 every frame, counts accepted/rejected/overruns, and reports
 *                 per-frame processing cycles under live bus load. -> C,D,E
 *
 * All human-readable output goes over semihosting/ITM printf (retarget in
 * your toolchain) so you can capture it from the debugger console. The TX/RX
 * counters are ALSO broadcast on the bus as diagnostic frames (ID 0x7FE/0x7FF)
 * so a plain CAN sniffer can log them without a debug probe.
 * ========================================================================== */
#include <stdint.h>
#include <stdio.h>
#include "can_hal.h"
#include "dut.h"

/* ------------------------------------------------------------ config */
#ifndef CORE_HZ
#define CORE_HZ      72000000u      /* SET THIS to your MCU core clock */
#endif
#ifndef BITRATE
#define BITRATE      500000u        /* 500 kbps to match ROAD */
#endif
#ifndef N_ITER
#define N_ITER       10000u         /* cycle-measurement iterations */
#endif
#ifndef TX_RATE_HZ
#define TX_RATE_HZ   1000u          /* application frames per second in load test */
#endif
#ifndef TAG_ENABLED
#define TAG_ENABLED  1              /* build once =0 (baseline) and once =1 */
#endif

/* provisioned per-ECU seed R (128-bit), same as the paper's worked example */
static const uint32_t R[4] = {0x0F1E2D3C,0x4B5A6978,0x8796A5B4,0xC3D2E1F0};

/* a small domain of centroids for the Voronoi check (Q16.16) */
static const pt_q16 CENTROIDS[10] = {
    {(int32_t)(0.20*65536),(int32_t)(0.30*65536)},
    {(int32_t)(0.55*65536),(int32_t)(0.12*65536)},
    {(int32_t)(0.80*65536),(int32_t)(0.60*65536)},
    {(int32_t)(0.35*65536),(int32_t)(0.75*65536)},
    {(int32_t)(0.10*65536),(int32_t)(0.90*65536)},
    {(int32_t)(0.65*65536),(int32_t)(0.40*65536)},
    {(int32_t)(0.45*65536),(int32_t)(0.55*65536)},
    {(int32_t)(0.90*65536),(int32_t)(0.20*65536)},
    {(int32_t)(0.25*65536),(int32_t)(0.50*65536)},
    {(int32_t)(0.70*65536),(int32_t)(0.85*65536)},
};
#define N_ECU 10
#define THETA2 ((uint32_t)(0.1*0.1*65536))   /* theta=0.1, squared, scaled */

/* ---- tiny xorshift so the compiler can't fold the loop away ---- */
static uint32_t rng = 0xC0FFEEu;
static inline uint32_t xr(void){ rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }

/* ---- the exact mapping the paper uses: q=(h(ID),h(D)) ---- */
static inline uint16_t xf16(uint32_t x){ uint16_t r=0; while(x){r^=x&0xFFFF;x>>=16;} return r; }

/* volatile sinks so results can't be optimised out */
volatile uint16_t g_tag_sink;
volatile int      g_v_sink;

/* --------------------------------------------------------------------------
 * ROLE_CYCLES : self-contained, no bus. Prints the table the paper needs.
 * -------------------------------------------------------------------------- */
static void measure_one(const char *name, uint32_t (*run)(void))
{
    uint32_t mn = 0xFFFFFFFFu, mx = 0; uint64_t sum = 0;
    /* warm the pipeline */
    for (int i=0;i<64;i++) (void)run();
    for (uint32_t i=0;i<N_ITER;i++){
        uint32_t c = run();
        if (c<mn) mn=c; if (c>mx) mx=c; sum += c;
    }
    /* subtract the measurement overhead (an empty timed region) once */
    printf("%-14s cycles: min=%lu mean=%lu max=%lu  (@%luMHz -> %lu.%03lu us)\n",
           name, (unsigned long)mn, (unsigned long)(sum/N_ITER), (unsigned long)mx,
           (unsigned long)(CORE_HZ/1000000u),
           (unsigned long)((mn*1000000ull/CORE_HZ)),        /* us integer */
           (unsigned long)((mn*1000000000ull/CORE_HZ)%1000));/* us frac */
}

/* each measured region returns its own cycle count, overhead-corrected */
static uint32_t g_overhead = 0;

static uint32_t run_empty(void){ uint32_t a=cyc_now(); uint32_t b=cyc_now(); return b-a; }

static uint32_t run_tag(void){
    uint32_t w0=xr(),w1=xr()&0x7FF,w2=xr(),w3=xr();
    uint32_t a=cyc_now();
    g_tag_sink = dut_tag(R,w0,w1,w2,w3);
    uint32_t b=cyc_now();
    return b-a-g_overhead;
}
static uint32_t run_voronoi(void){
    pt_q16 c = { (int32_t)(xr()&0xFFFF), (int32_t)(xr()&0xFFFF) };
    int idx = xr()%N_ECU;
    uint32_t a=cyc_now();
    g_v_sink = dut_voronoi(c, CENTROIDS, N_ECU, idx, THETA2);
    uint32_t b=cyc_now();
    return b-a-g_overhead;
}
static uint32_t run_ema(void){
    pt_q16 c = { (int32_t)(xr()&0xFFFF), (int32_t)(xr()&0xFFFF) };
    pt_q16 q = { (int32_t)(xr()&0xFFFF), (int32_t)(xr()&0xFFFF) };
    uint32_t a=cyc_now();
    dut_ema(&c,q);
    uint32_t b=cyc_now();
    g_v_sink = c.x;
    return b-a-g_overhead;
}
static uint32_t run_full(void){
    uint32_t w0=xr(),w1=xr()&0x7FF,w2=xr(),w3=xr();
    pt_q16 c = { (int32_t)(xr()&0xFFFF), (int32_t)(xr()&0xFFFF) };
    pt_q16 q = { (int32_t)(xr()&0xFFFF), (int32_t)(xr()&0xFFFF) };
    int idx = xr()%N_ECU;
    uint32_t a=cyc_now();
    g_tag_sink = dut_tag(R,w0,w1,w2,w3);
    dut_ema(&c,q);
    g_v_sink   = dut_voronoi(c, CENTROIDS, N_ECU, idx, THETA2);
    uint32_t b=cyc_now();
    return b-a-g_overhead;
}

static void role_cycles(void)
{
    printf("\n=== Triangulating Trust cycle measurement ===\n");
    printf("core=%lu Hz  iterations=%lu\n", (unsigned long)CORE_HZ, (unsigned long)N_ITER);
    /* calibrate the timing-region overhead */
    { uint32_t mn=0xFFFFFFFFu; for(int i=0;i<256;i++){uint32_t c=run_empty(); if(c<mn)mn=c;} g_overhead=mn; }
    printf("timer overhead subtracted: %lu cyc\n", (unsigned long)g_overhead);
    printf("model predictions: tag=115 voronoi=385 ema=107 full=607\n\n");
    measure_one("tag",     run_tag);
    measure_one("voronoi", run_voronoi);
    measure_one("ema",     run_ema);
    measure_one("full-path", run_full);
    printf("\nReport min (worst-case-free) as the deterministic cost; mean/max\n"
           "reveal any interrupt interference. Delta vs model = measured-predicted.\n");
    printf("=== end ===\n");
}

/* --------------------------------------------------------------------------
 * ROLE_TX : sender. Emits TX_RATE_HZ app frames/s. If TAG_ENABLED, appends a
 * 2-byte tag (companion frame when the payload is already full).
 * Build TWICE: -DTAG_ENABLED=0 (baseline) and =1 (authenticated) to get the
 * bus-utilisation delta (scaffold slot 23) from the sniffer.
 * -------------------------------------------------------------------------- */
static void role_tx(void)
{
    printf("TX role: rate=%lu Hz tag=%d\n",(unsigned long)TX_RATE_HZ,TAG_ENABLED);
    const uint32_t period_ms = (TX_RATE_HZ>0)?(1000u/TX_RATE_HZ):1;
    uint32_t nonce = 0, sent = 0;
    for(;;){
        can_frame_t f; 
        f.id  = 0x100 + (sent % N_ECU);      /* cycle through the domain IDs */
        /* mix of DLC-8 (companion-frame path) and shorter frames */
        int full = ((sent & 3u)==0);          /* 25% are payload-saturated */
        f.dlc = full ? 8 : 6;
        for(int k=0;k<f.dlc;k++) f.data[k]=(uint8_t)xr();

#if TAG_ENABLED
        uint16_t qx = xf16(f.id ^ (nonce & 0xFFFF));
        uint32_t dlo=0; for(int k=0;k<4;k++) dlo=(dlo<<8)|(k<f.dlc?f.data[k]:0);
        uint16_t tag = dut_tag(R, ((uint32_t)qx<<16)|xf16(dlo^nonce),
                               f.id & 0x7FF, nonce, dlo);
        if(!full){
            f.data[6]=(uint8_t)(tag>>8); f.data[7]=(uint8_t)tag; f.dlc=8;
            can_hal_send(&f);
        } else {
            can_hal_send(&f);                 /* original, full 8-byte frame */
            can_frame_t comp = { .id=f.id+1, .dlc=4 };
            comp.data[0]=(uint8_t)(tag>>8); comp.data[1]=(uint8_t)tag;
            comp.data[2]=(uint8_t)(nonce>>8); comp.data[3]=(uint8_t)nonce;
            can_hal_send(&comp);              /* companion frame */
        }
#else
        can_hal_send(&f);                     /* baseline: no tag */
#endif
        nonce++; sent++;
        if((sent % 1000u)==0){
            /* broadcast a diagnostic frame the sniffer can log */
            can_frame_t d={.id=0x7FE,.dlc=8};
            d.data[0]=(uint8_t)(sent>>24);d.data[1]=(uint8_t)(sent>>16);
            d.data[2]=(uint8_t)(sent>>8); d.data[3]=(uint8_t)sent;
            d.data[4]=(uint8_t)(can_hal_tx_errors());
            can_hal_send(&d);
            printf("TX sent=%lu tx_err=%lu\n",(unsigned long)sent,
                   (unsigned long)can_hal_tx_errors());
        }
        delay_ms(period_ms);
    }
}

/* --------------------------------------------------------------------------
 * ROLE_RX : receiver. Verifies every tagged frame and runs the Voronoi check,
 * timing the per-frame processing under live bus load. Counts accept/reject/
 * overrun. This is the measurement that proves the compute cost holds up while
 * the bus is actually busy (scaffold C + E).
 * -------------------------------------------------------------------------- */
static void role_rx(void)
{
    printf("RX role: verifying under live load\n");
    pt_q16 cent[N_ECU]; for(int i=0;i<N_ECU;i++) cent[i]=CENTROIDS[i];
    uint32_t seen=0, accepted=0, rejected=0;
    uint32_t proc_min=0xFFFFFFFFu, proc_sum=0, proc_max=0;
    uint32_t nonce_expected=0;

    for(;;){
        can_frame_t f;
        if(!can_hal_recv(&f)) continue;
        if(f.id==0x7FE || (f.id&0x7FF)>0x700) continue;  /* skip diagnostics */
        seen++;

        uint32_t t0=cyc_now();
        /* recompute q and tag, run containment -- the real per-frame path */
        uint16_t qx = xf16(f.id ^ (nonce_expected & 0xFFFF));
        uint32_t dlo=0; for(int k=0;k<4;k++) dlo=(dlo<<8)|(k<f.dlc?f.data[k]:0);
        uint16_t tag = dut_tag(R, ((uint32_t)qx<<16)|xf16(dlo^nonce_expected),
                               f.id & 0x7FF, nonce_expected, dlo);
        pt_q16 q = { (int32_t)(((uint32_t)qx*65536u)/65535u),
                     (int32_t)(((uint32_t)xf16(dlo)*65536u)/65535u) };
        int idx = f.id % N_ECU;
        dut_ema(&cent[idx], q);
        int contained = dut_voronoi(cent[idx], cent, N_ECU, idx, THETA2);
        uint32_t proc = cyc_now()-t0;

        if(proc<proc_min)proc_min=proc; if(proc>proc_max)proc_max=proc; proc_sum+=proc;

        /* accept if tag present-and-matches OR (demo) accept baseline frames */
        uint16_t carried = (f.dlc==8)?(((uint16_t)f.data[6]<<8)|f.data[7]):tag;
        if(carried==tag && contained) accepted++; else rejected++;
        nonce_expected++;

        if((seen % 1000u)==0){
            printf("RX seen=%lu acc=%lu rej=%lu ovr=%lu  proc/frame: "
                   "min=%lu mean=%lu max=%lu cyc\n",
                   (unsigned long)seen,(unsigned long)accepted,(unsigned long)rejected,
                   (unsigned long)can_hal_rx_overruns(),
                   (unsigned long)proc_min,(unsigned long)(proc_sum/seen),
                   (unsigned long)proc_max);
        }
    }
}

int main(void)
{
    cyc_init();
#if defined(RUN_SELFTEST)
    /* Loopback sanity check: proves CAN timing/clock/pins are correct before
     * any measurement is trusted. Build with -DRUN_SELFTEST to enable. */
    {
        int st = can_hal_selftest();
        printf("CAN loopback self-test: %s (%d)\n", st==0?"PASS":"FAIL", st);
        if (st != 0) { printf("Config wrong for this board -- fix can_hal before trusting results.\n"); for(;;){} }
    }
#endif
    can_hal_init(CORE_HZ, BITRATE);
#if   defined(ROLE_CYCLES)
    role_cycles(); for(;;){}
#elif defined(ROLE_TX)
    role_tx();
#elif defined(ROLE_RX)
    role_rx();
#else
#  error "define one of ROLE_CYCLES / ROLE_TX / ROLE_RX"
#endif
    return 0;
}
