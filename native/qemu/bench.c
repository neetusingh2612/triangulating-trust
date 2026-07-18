#include <stdint.h>
#include <string.h>
uint16_t hmac_can_tag(const uint8_t key[20], const uint8_t msg14[14]);
uint16_t tt_tag_unrolled(const uint32_t *Rk, uint32_t w0,uint32_t w1,uint32_t w2,uint32_t w3);
volatile uint16_t sink;
volatile uint32_t MARK;   /* write here to bracket the region of interest */
int main(void){
  uint8_t key[20]; uint8_t msg[14];
  for(int i=0;i<20;i++) key[i]=0xAA;
  for(int i=0;i<14;i++) msg[i]=0x55;
  uint32_t R[4]={0x0F1E2D3C,0x4B5A6978,0x8796A5B4,0xC3D2E1F0};

  MARK=1;                       /* ---- START HMAC ---- */
  sink = hmac_can_tag(key,msg);
  MARK=2;                       /* ---- END HMAC ---- */

  MARK=3;                       /* ---- START TT ---- */
  sink = tt_tag_unrolled(R,0x11223344,0x123,0x5,0x10200000);
  MARK=4;                       /* ---- END TT ---- */
  return 0;
}
