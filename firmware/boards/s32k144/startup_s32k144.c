/* startup_s32k144.c -- minimal vector table + reset for S32K144 (Cortex-M4).
 * NOTE: the S32K watchdog (WDOG) is enabled out of reset and will reset the
 * part in ~256ms unless disabled or serviced. We disable it first thing.
 * [VERIFY] WDOG unlock sequence against the S32K1xx RM. */
#include <stdint.h>
extern uint32_t _sidata,_sdata,_edata,_sbss,_ebss,_estack;
extern uint32_t _siramfunc,_sramfunc,_eramfunc;
int main(void);

#define WDOG_CNT  (*(volatile uint32_t*)0x40052000u)
#define WDOG_TOVAL (*(volatile uint32_t*)0x40052008u)
#define WDOG_CS   (*(volatile uint32_t*)0x40052004u)

static void wdog_disable(void){
    WDOG_CNT = 0xD928C520u;            /* unlock sequence [VERIFY] */
    WDOG_TOVAL = 0x0000FFFFu;
    WDOG_CS = (1u<<5);                 /* CS[UPDATE]=1, EN=0 -> disabled [VERIFY] */
}

void Reset_Handler(void){
    wdog_disable();
    uint32_t *s,*d;
    for(s=&_sidata,d=&_sdata; d<&_edata;) *d++=*s++;
    for(s=&_siramfunc,d=&_sramfunc; d<&_eramfunc;) *d++=*s++;
    for(d=&_sbss; d<&_ebss;) *d++=0;
    main(); for(;;){}
}
void Default_Handler(void){ for(;;){} }

__attribute__((section(".isr_vector"),used))
void(* const vtab[])(void)={ (void(*)(void))&_estack, Reset_Handler,
    Default_Handler,Default_Handler,Default_Handler,Default_Handler,Default_Handler };
