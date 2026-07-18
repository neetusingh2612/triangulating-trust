#include <stdint.h>
extern int main(void);
extern uint32_t _estack;
void Reset_Handler(void){ main(); for(;;); }
__attribute__((section(".isr_vector"), used))
void (* const vtab[])(void) = { (void(*)(void))&_estack, Reset_Handler };

/* minimal libc bits; these ARE part of HMAC's real cost and are counted */
void *memset(void *d, int c, unsigned n){unsigned char*p=d;while(n--)*p++=(unsigned char)c;return d;}
void *memcpy(void *d, const void *s, unsigned n){unsigned char*p=d;const unsigned char*q=s;while(n--)*p++=*q++;return d;}
