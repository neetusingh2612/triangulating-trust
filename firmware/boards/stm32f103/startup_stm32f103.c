/* startup_stm32f103.c -- minimal vector table + reset for bare-metal build */
#include <stdint.h>
extern uint32_t _sidata,_sdata,_edata,_sbss,_ebss,_estack;
extern uint32_t _siramfunc,_sramfunc,_eramfunc;   /* .ramfunc copy region */
int main(void);
void Reset_Handler(void){
    uint32_t *s,*d;
    for(s=&_sidata,d=&_sdata; d<&_edata;) *d++=*s++;      /* .data */
    for(s=&_siramfunc,d=&_sramfunc; d<&_eramfunc;) *d++=*s++; /* .ramfunc -> RAM */
    for(d=&_sbss; d<&_ebss;) *d++=0;                       /* .bss */
    main(); for(;;){}
}
void Default_Handler(void){ for(;;){} }
/* retarget printf to nowhere by default; wire to ITM/semihosting on the bench */

__attribute__((section(".isr_vector"),used))
void(* const vtab[])(void)={ (void(*)(void))&_estack, Reset_Handler,
    Default_Handler,Default_Handler,Default_Handler,Default_Handler,Default_Handler };
