/* ==========================================================================
 * can_hal_stm32f103.c -- bxCAN + TJA1050 backend for STM32F103 ("Blue Pill").
 *
 * Bare-metal register access, no vendor HAL dependency, so it compiles with a
 * plain arm-none-eabi-gcc. Assumes:
 *   - 8 MHz HSE crystal, PLL x9 -> 72 MHz SYSCLK, APB1 = 36 MHz (CAN clock)
 *   - CAN on PB8(RX)/PB9(TX) with remap, TJA1050 transceiver
 *   - 500 kbps: BRP so that tq gives 500k with 1+BS1+BS2 = 8 tq at 36MHz/9=4MHz
 *
 * If your board differs (different crystal, pins, or transceiver), THIS is the
 * file to edit -- nothing else. Every register write is commented.
 *
 * NOTE: register definitions are inlined so this builds without a device
 * header. For production use ST's CMSIS device header instead and delete the
 * REG block below.
 * ========================================================================== */
#include "can_hal.h"

/* ---- minimal register map (STM32F103, bxCAN) ---- */
#define PERIPH   0x40000000u
#define RCC      (*(volatile uint32_t*)0x40021000u)      /* base */
#define RCC_CR       (*(volatile uint32_t*)0x40021000u)
#define RCC_CFGR     (*(volatile uint32_t*)0x40021004u)
#define RCC_APB1ENR  (*(volatile uint32_t*)0x4002101Cu)
#define RCC_APB2ENR  (*(volatile uint32_t*)0x40021018u)
#define FLASH_ACR    (*(volatile uint32_t*)0x40022000u)
#define AFIO_MAPR    (*(volatile uint32_t*)0x40010004u)
#define GPIOB_CRH    (*(volatile uint32_t*)0x40010C04u)

#define CAN1     0x40006400u
#define CAN_MCR  (*(volatile uint32_t*)(CAN1+0x00))
#define CAN_MSR  (*(volatile uint32_t*)(CAN1+0x04))
#define CAN_TSR  (*(volatile uint32_t*)(CAN1+0x08))
#define CAN_RF0R (*(volatile uint32_t*)(CAN1+0x0C))
#define CAN_ESR  (*(volatile uint32_t*)(CAN1+0x18))
#define CAN_BTR  (*(volatile uint32_t*)(CAN1+0x1C))
#define CAN_TI0R (*(volatile uint32_t*)(CAN1+0x180))
#define CAN_TDT0R (*(volatile uint32_t*)(CAN1+0x184))
#define CAN_TDL0R (*(volatile uint32_t*)(CAN1+0x188))
#define CAN_TDH0R (*(volatile uint32_t*)(CAN1+0x18C))
#define CAN_RI0R (*(volatile uint32_t*)(CAN1+0x1B0))
#define CAN_RDT0R (*(volatile uint32_t*)(CAN1+0x1B4))
#define CAN_RDL0R (*(volatile uint32_t*)(CAN1+0x1B8))
#define CAN_RDH0R (*(volatile uint32_t*)(CAN1+0x1BC))
#define CAN_FMR  (*(volatile uint32_t*)(CAN1+0x200))
#define CAN_FA1R (*(volatile uint32_t*)(CAN1+0x21C))
#define CAN_F0R1 (*(volatile uint32_t*)(CAN1+0x240))
#define CAN_F0R2 (*(volatile uint32_t*)(CAN1+0x244))

/* ---- ARMv7-M DWT cycle counter ---- */
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004u)
#define DEMCR      (*(volatile uint32_t*)0xE000EDFCu)

static volatile uint32_t s_rx_overruns = 0;
static uint32_t s_core_hz = 72000000u;

void cyc_init(void){
    DEMCR |= (1u<<24);        /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;           /* CYCCNTENA */
}
uint32_t cyc_now(void){ return DWT_CYCCNT; }

void delay_ms(uint32_t ms){
    /* crude but adequate for load pacing; ~ (core/4) loops per ms */
    volatile uint32_t n = (s_core_hz/4000u)*ms;
    while(n--) __asm__ volatile("nop");
}

static void clock_72mhz(void){
    FLASH_ACR = (FLASH_ACR & ~7u) | 2u | (1u<<4);   /* 2 wait states, prefetch */
    RCC_CR |= (1u<<16);                              /* HSEON */
    while(!(RCC_CR & (1u<<17))){}                    /* HSERDY */
    /* PLL src=HSE, PLLMUL=x9 -> 8*9=72MHz; APB1=/2=36MHz, APB2=/1 */
    RCC_CFGR = (RCC_CFGR & ~0x3FFFFFu)
             | (1u<<16)          /* PLLSRC=HSE */
             | (7u<<18)          /* PLLMUL=9 (0b0111) */
             | (4u<<8);          /* PPRE1=/2 */
    RCC_CR |= (1u<<24);                              /* PLLON */
    while(!(RCC_CR & (1u<<25))){}                    /* PLLRDY */
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;                /* SW=PLL */
    while(((RCC_CFGR>>2)&3u)!=2u){}                  /* SWS=PLL */
    s_core_hz = 72000000u;
}

int can_hal_init(uint32_t core_hz, uint32_t bitrate_bps){
    (void)core_hz; (void)bitrate_bps;   /* fixed config in this reference */
    clock_72mhz();
    RCC_APB2ENR |= (1u<<3) | (1u<<0);   /* IOPBEN, AFIOEN */
    RCC_APB1ENR |= (1u<<25);            /* CAN1EN */

    /* PB8=CAN_RX (input pull-up), PB9=CAN_TX (AF push-pull 50MHz), with remap */
    AFIO_MAPR = (AFIO_MAPR & ~(3u<<13)) | (2u<<13);  /* CAN_REMAP=10 -> PB8/PB9 */
    GPIOB_CRH &= ~((0xFu<<0) | (0xFu<<4));
    GPIOB_CRH |=  (0x8u<<0);            /* PB8 input pull-up/down */
    GPIOB_CRH |=  (0xBu<<4);            /* PB9 AF push-pull 50MHz */

    /* enter init mode */
    CAN_MCR &= ~(1u<<1);               /* SLEEP=0 */
    CAN_MCR |= (1u<<0);                /* INRQ=1 */
    while(!(CAN_MSR & (1u<<0))){}       /* INAK */

    /* 500 kbps @ APB1=36MHz: BRP=? tq = (BRP+1)/36MHz.
     * Choose BRP=3 -> 4 tq per us? Use classic: 36MHz/(BRP+1)/(1+BS1+BS2)=500k.
     * With BRP+1=4 -> 9MHz; /18 tq = 500k -> 1+BS1+BS2=18: BS1=15,BS2=2. */
    CAN_BTR = (0u<<30)                 /* normal mode (set (1<<30) for silent, (1<<31) loopback) */
            | ((2u-1u)<<20)            /* TS2 = 2 -> BS2=2 */
            | ((15u-1u)<<16)           /* TS1 = 15 -> BS1=15 */
            | ((1u-1u)<<24)            /* SJW=1 */
            | (4u-1u);                 /* BRP=4 -> prescaler 4 */

    CAN_MCR |= (1u<<6);                /* ABOM: auto bus-off recovery */

    /* leave init */
    CAN_MCR &= ~(1u<<0);               /* INRQ=0 */
    while(CAN_MSR & (1u<<0)){}          /* wait INAK clear */

    /* filter 0: accept all into FIFO0 */
    CAN_FMR |= 1u;                     /* FINIT */
    CAN_FA1R &= ~1u;                   /* deactivate f0 */
    CAN_F0R1 = 0; CAN_F0R2 = 0;        /* mask mode, mask=0 -> match all */
    CAN_FA1R |= 1u;                    /* activate f0 */
    CAN_FMR &= ~1u;                    /* filters active */
    return 0;
}

int can_hal_send(const can_frame_t *f){
    uint32_t spin=200000u;
    while(!(CAN_TSR & (1u<<26))){ if(!--spin) return -1; }   /* TME0 */
    CAN_TDT0R = (f->dlc & 0xF);
    uint32_t l=0,h=0;
    for(int i=0;i<4;i++) if(i<f->dlc) l|=(uint32_t)f->data[i]<<(8*i);
    for(int i=4;i<8;i++) if(i<f->dlc) h|=(uint32_t)f->data[i]<<(8*(i-4));
    CAN_TDL0R=l; CAN_TDH0R=h;
    CAN_TI0R = ((f->id & 0x7FF)<<21) | 1u;   /* STID + TXRQ, standard/data */
    return 0;
}

int can_hal_recv(can_frame_t *f){
    if(((CAN_RF0R)&3u)==0) return 0;          /* FMP0: messages pending */
    uint32_t ri=CAN_RI0R, rdt=CAN_RDT0R, l=CAN_RDL0R, h=CAN_RDH0R;
    f->id  = (ri>>21)&0x7FF;
    f->dlc = rdt & 0xF;
    for(int i=0;i<4;i++) f->data[i]  =(uint8_t)(l>>(8*i));
    for(int i=4;i<8;i++) f->data[i]  =(uint8_t)(h>>(8*(i-4)));
    if(CAN_RF0R & (1u<<4)) s_rx_overruns++;    /* FOVR0 */
    CAN_RF0R |= (1u<<5);                        /* RFOM0: release FIFO */
    return 1;
}

uint32_t can_hal_tx_errors(void){ return (CAN_ESR>>16)&0xFF; }  /* TEC */
uint32_t can_hal_rx_overruns(void){ return s_rx_overruns; }
