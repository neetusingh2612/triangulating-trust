/* ==========================================================================
 * can_hal_s32k144.c -- FlexCAN0 backend for NXP S32K144EVB-Q100.
 *
 * Cortex-M4F @ 80 MHz (normal RUN). On-board TJA1057GT CAN-FD transceiver on
 * FlexCAN0, pins PTE4 (CAN0_RX) / PTE5 (CAN0_TX). Classic CAN, 500 kbps to
 * match the ROAD dataset.
 *
 * ############################################################################
 * # THIS FILE IS WRITTEN FROM THE S32K1xx REFERENCE MANUAL, NOT TESTED ON    #
 * # SILICON HERE. Before trusting a single measurement, VERIFY every item    #
 * # tagged [VERIFY] against:                                                  #
 * #   - S32K1xx Reference Manual (RM), rev 14 (register offsets, bit fields)  #
 * #   - S32K144EVB-Q100 schematic, YOUR board rev (pins, transceiver, xtal)  #
 * # A one-off loopback self-test (LPB=1) is provided: call can_hal_selftest()#
 * # first; if it fails, the config below is wrong for your board and NOTHING #
 * # downstream is valid.                                                      #
 * ############################################################################
 *
 * The clean way to do this on S32K is the NXP S32 SDK / S32DS FlexCAN driver
 * (config in the Peripherals tool, a few API calls). This bare-metal version
 * exists so the firmware builds with plain arm-none-eabi-gcc and so every
 * register write is visible. If you have S32DS, prefer wiring can_hal.h to the
 * SDK's FLEXCAN_DRV_* calls instead -- it is less error-prone than the below.
 * ========================================================================== */
#include "can_hal.h"

/* ---- register map (S32K1xx RM). [VERIFY] all offsets against rev 14. ---- */
#define SCG      0x40064000u
#define SCG_SOSCCSR (*(volatile uint32_t*)(SCG+0x100))
#define SCG_SOSCDIV (*(volatile uint32_t*)(SCG+0x104))
#define SCG_SOSCCFG (*(volatile uint32_t*)(SCG+0x108))

#define PCC      0x40065000u
#define PCC_FLEXCAN0 (*(volatile uint32_t*)(PCC+0x90))   /* [VERIFY] offset */
#define PCC_PORTE    (*(volatile uint32_t*)(PCC+0x140))  /* [VERIFY] offset */

#define PORTE    0x4004D000u
#define PORTE_PCR4 (*(volatile uint32_t*)(PORTE+0x10))   /* PTE4 = CAN0_RX */
#define PORTE_PCR5 (*(volatile uint32_t*)(PORTE+0x14))   /* PTE5 = CAN0_TX */

#define CAN0     0x40024000u
#define CAN_MCR   (*(volatile uint32_t*)(CAN0+0x00))
#define CAN_CTRL1 (*(volatile uint32_t*)(CAN0+0x04))
#define CAN_TIMER (*(volatile uint32_t*)(CAN0+0x08))
#define CAN_RXMGMASK (*(volatile uint32_t*)(CAN0+0x10))
#define CAN_ESR1  (*(volatile uint32_t*)(CAN0+0x20))
#define CAN_IMASK1 (*(volatile uint32_t*)(CAN0+0x28))
#define CAN_IFLAG1 (*(volatile uint32_t*)(CAN0+0x30))
#define CAN_ECR   (*(volatile uint32_t*)(CAN0+0x1C))     /* error counters */
/* Message buffers: 16 bytes each from offset 0x80. MB[k] word view. */
#define MB_BASE   (CAN0+0x80)
#define MBw(k,w)  (*(volatile uint32_t*)(MB_BASE + (k)*16u + (w)*4u))

/* ---- ARMv7-M DWT ---- */
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004u)
#define DEMCR      (*(volatile uint32_t*)0xE000EDFCu)

/* MB assignment: MB0 = TX, MB4 = RX (classic FlexCAN convention). */
#define TX_MB 0u
#define RX_MB 4u

/* CODE field values (MB CS word, bits 24-27) */
#define CODE_TX_INACTIVE 0x8u
#define CODE_TX_DATA     0xCu
#define CODE_RX_EMPTY    0x4u
#define CODE_RX_FULL     0x2u

static volatile uint32_t s_rx_overruns = 0;
static uint32_t s_core_hz = 80000000u;

void cyc_init(void){ DEMCR |= (1u<<24); DWT_CYCCNT=0; DWT_CTRL |= 1u; }
uint32_t cyc_now(void){ return DWT_CYCCNT; }
void delay_ms(uint32_t ms){ volatile uint32_t n=(s_core_hz/4000u)*ms; while(n--) __asm__ volatile("nop"); }

/* --------------------------------------------------------------------------
 * NOTE ON CLOCKING: this reference assumes the SPLL/normal-RUN 80 MHz setup
 * done by the S32DS "clock_config.c" the EVB ships with. Bare-metal SPLL
 * bring-up is long and board-specific; DO NOT reimplement it here blindly.
 * Use the EVB's generated clock init, then call can_hal_init(). We only bring
 * up SOSC (the 8 MHz crystal) as the FlexCAN protocol clock source. [VERIFY]
 * -------------------------------------------------------------------------- */
static void sosc_8mhz(void){
    /* [VERIFY] SOSC config for the EVB's 8 MHz crystal. */
    SCG_SOSCDIV = (1u<<0) | (1u<<8);   /* DIV1=DIV2=/1 */
    SCG_SOSCCFG = (1u<<2) | (1u<<4);   /* EREFS=1 (xtal), RANGE=medium [VERIFY] */
    SCG_SOSCCSR = (1u<<0);             /* SOSCEN */
    while(!(SCG_SOSCCSR & (1u<<24))){} /* SOSCVLD */
}

static int flexcan_common_init(uint32_t ctrl1_timing, int loopback){
    /* clock the peripheral: FlexCAN protocol clock = SOSC (8 MHz) [VERIFY src] */
    PCC_FLEXCAN0 &= ~(1u<<30);          /* disable while configuring (CGC=0) */
    PCC_FLEXCAN0  =  (1u<<30);          /* CGC=1 enable clock */

    /* pins: PTE4/PTE5 -> ALT5 (CAN0). [VERIFY] MUX value on your package. */
    PCC_PORTE |= (1u<<30);              /* clock PORTE */
    PORTE_PCR4 = (5u<<8);               /* MUX=101 ALT5 = CAN0_RX */
    PORTE_PCR5 = (5u<<8);               /* MUX=101 ALT5 = CAN0_TX */

    /* enable module, then enter freeze to configure */
    CAN_MCR &= ~(1u<<31);               /* MDIS=0 enable */
    while(CAN_MCR & (1u<<28)){}         /* wait LPMACK clear */
    CAN_MCR |= (1u<<25) | (1u<<24);     /* FRZ + HALT -> request freeze */
    while(!(CAN_MCR & (1u<<24))){}      /* FRZACK */

    /* select protocol clock = oscillator (CLKSRC=0). [VERIFY] */
    CAN_CTRL1 &= ~(1u<<13);             /* CLKSRC = 0 (oscillator) */

    /* soft reset */
    CAN_MCR |= (1u<<25);
    /* bit timing: caller supplies CTRL1 timing bits (PRESDIV/PROPSEG/PSEG1/2/RJW) */
    CAN_CTRL1 = (CAN_CTRL1 & ~0xFFFFFF00u) | (ctrl1_timing & 0xFFFFFF00u);
    if(loopback) CAN_CTRL1 |= (1u<<12); /* LPB=1 loopback for self-test */
    else         CAN_CTRL1 &= ~(1u<<12);

    /* number of MBs, and individual masking */
    CAN_MCR = (CAN_MCR & ~0x7Fu) | 15u; /* MAXMB = 15 (16 MBs) */
    CAN_MCR |= (1u<<16);                /* IRMQ = individual RX masking */
    CAN_RXMGMASK = 0;                   /* accept all IDs into RX MB */

    /* init all MBs to inactive */
    for(int k=0;k<16;k++){ MBw(k,0)=0; MBw(k,1)=0; MBw(k,2)=0; MBw(k,3)=0; }

    /* set up RX MB: standard ID, empty */
    MBw(RX_MB,0) = (CODE_RX_EMPTY<<24);
    MBw(RX_MB,1) = 0;                   /* ID (masked to accept all) */

    /* exit freeze -> start */
    CAN_MCR &= ~((1u<<25)|(1u<<24));    /* clear FRZ+HALT */
    while(CAN_MCR & (1u<<24)){}         /* wait FRZACK clear */
    while(CAN_MCR & (1u<<20)){}         /* wait NOTRDY clear (module ready) */
    return 0;
}

/* 500 kbps @ 8 MHz FlexCAN clock:
 *   Tq = (PRESDIV+1)/8MHz. Choose PRESDIV=0 -> Tq=125ns -> 16 Tq/bit = 500k.
 *   16 Tq = 1(SYNC) + PROPSEG+1 + PSEG1+1 + PSEG2+1.
 *   Pick PROPSEG=6, PSEG1=4, PSEG2=1 -> 1+7+5+2 = 15... adjust to 16:
 *   PROPSEG=7,PSEG1=4,PSEG2=1 -> 1+8+5+2 = 16. RJW=1. [VERIFY sample point]
 * CTRL1 layout: PRESDIV[31:24] RJW[23:22] PSEG1[21:19] PSEG2[18:16] ...
 *               PROPSEG[2:0]. [VERIFY exact bit positions against RM.]
 */
static uint32_t timing_500k_8mhz(void){
    uint32_t PRESDIV=0, RJW=1, PSEG1=4, PSEG2=1, PROPSEG=7;
    return (PRESDIV<<24)|(RJW<<22)|(PSEG1<<19)|(PSEG2<<16)|(PROPSEG<<0);
}

int can_hal_init(uint32_t core_hz, uint32_t bitrate_bps){
    (void)bitrate_bps;                 /* fixed 500k in this reference */
    s_core_hz = core_hz ? core_hz : 80000000u;
    sosc_8mhz();
    return flexcan_common_init(timing_500k_8mhz(), /*loopback=*/0);
}

/* Loopback self-test: transmit one frame to ourselves. Returns 0 if the frame
 * comes back, <0 otherwise. RUN THIS FIRST on the bench. */
int can_hal_selftest(void){
    sosc_8mhz();
    flexcan_common_init(timing_500k_8mhz(), /*loopback=*/1);
    can_frame_t tx={.id=0x123,.dlc=2,.data={0xAB,0xCD}}, rx;
    can_hal_send(&tx);
    for(volatile uint32_t s=0; s<1000000u; s++){
        if(can_hal_recv(&rx)){
            return (rx.id==0x123 && rx.dlc==2 &&
                    rx.data[0]==0xAB && rx.data[1]==0xCD) ? 0 : -2;
        }
    }
    return -1;   /* nothing looped back -> timing/clock/pin config is wrong */
}

int can_hal_send(const can_frame_t *f){
    /* wait TX MB free */
    uint32_t spin=200000u;
    while(((MBw(TX_MB,0)>>24)&0xF)==CODE_TX_DATA){ if(!--spin) return -1; }
    uint32_t w2=0,w3=0;                 /* FlexCAN stores data big-endian in MB */
    for(int i=0;i<4;i++) if(i<f->dlc) w2 |= (uint32_t)f->data[i]     << (24-8*i);
    for(int i=4;i<8;i++) if(i<f->dlc) w3 |= (uint32_t)f->data[i]     << (24-8*(i-4));
    MBw(TX_MB,1) = (f->id & 0x7FF) << 18;      /* standard ID in [28:18] */
    MBw(TX_MB,2) = w2;
    MBw(TX_MB,3) = w3;
    /* CS: CODE=TX_DATA, SRR=1, IDE=0, DLC */
    MBw(TX_MB,0) = (CODE_TX_DATA<<24) | (1u<<22) | ((f->dlc & 0xF)<<16);
    return 0;
}

int can_hal_recv(can_frame_t *f){
    if(!(CAN_IFLAG1 & (1u<<RX_MB))) return 0;   /* no frame in RX MB */
    uint32_t cs = MBw(RX_MB,0);
    uint32_t id = MBw(RX_MB,1);
    uint32_t w2 = MBw(RX_MB,2), w3 = MBw(RX_MB,3);
    f->id  = (id>>18) & 0x7FF;
    f->dlc = (cs>>16) & 0xF;
    for(int i=0;i<4;i++) f->data[i]  = (uint8_t)(w2 >> (24-8*i));
    for(int i=4;i<8;i++) f->data[i]  = (uint8_t)(w3 >> (24-8*(i-4)));
    if(((cs>>24)&0xF)==CODE_RX_FULL){ /* fine */ }
    (void)CAN_TIMER;                    /* reading TIMER unlocks the MB */
    CAN_IFLAG1 = (1u<<RX_MB);           /* clear flag (w1c) */
    MBw(RX_MB,0) = (CODE_RX_EMPTY<<24); /* re-arm */
    if(CAN_ESR1 & (1u<<17)) s_rx_overruns++;  /* [VERIFY] RX overrun bit */
    return 1;
}

uint32_t can_hal_tx_errors(void){ return (CAN_ECR>>0)&0xFF; }   /* TXERRCNT [VERIFY] */
uint32_t can_hal_rx_overruns(void){ return s_rx_overruns; }
