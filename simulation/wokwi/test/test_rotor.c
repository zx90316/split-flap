// Tests the same C callbacks uploaded to Wokwi, through a small pin/framebuffer double.
#include <assert.h>
#include <stdio.h>
#include "wokwi-api.h"
int values[32],modes[32];
#include "../unipolar.chip.c"
static void coils(chip_state_t*s,int mask){for(int i=0;i<4;i++)values[s->in[i]]=(mask>>i)&1;sample(s);}
int main(void){
 chip_state_t s={0};s.phase=-1;s.revolution=4096;s.hall_width=32;s.position=100;s.common=4;s.hall=5;
 for(int i=0;i<4;i++){s.in[i]=i;}
 values[4]=1;update_hall(&s);assert(modes[5]==INPUT);
 coils(&s,sequence[0]);assert(s.position==100);
 for(int i=1;i<=4096;i++)coils(&s,sequence[i%8]);
 assert(s.position==100 && s.errors==0);
 for(int i=1;i<=100;i++)coils(&s,sequence[(8-i%8)%8]);
 assert(s.position==0 && modes[5]==OUTPUT && values[5]==0);
 int phase=s.phase;coils(&s,0);assert(s.position==0 && s.phase==phase);
 coils(&s,sequence[phase]);assert(s.position==0);
 coils(&s,sequence[(phase+1)%8]);assert(s.position==1 && s.errors==0);
 coils(&s,15);assert(s.errors==1);
 values[4]=0;coils(&s,1);assert(s.mask==0 && s.position==1);
 puts("PASS Wokwi rotor C callbacks: forward/reverse, 4096 wrap, automatic open-drain Hall, coil release, invalid phase and motor rail off");
 return 0;
}
