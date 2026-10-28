// SPDX-License-Identifier: GPL-3.0-only
// Combined ideal ULN2003 low-side sinks + four-coil unipolar phase observer.
// Ideal shaft position drives active-low/open-drain HALL.
// NO electrical/torque/inertia model. 4096 is an unmeasured default assumption.
#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef struct {
  pin_t in[4],common,hall;
  timer_t settle;
  buffer_t framebuffer;
  uint8_t pixels[96*96*4];
  uint8_t mask;
  int phase, position;
  uint32_t errors, unit, revolution, hall_width;
} chip_state_t;
static const uint8_t sequence[8]={1,3,2,6,4,12,8,9};
static void pixel(chip_state_t *s,int x,int y,uint8_t r,uint8_t g,uint8_t b) {
  if(x<0 || x>=96 || y<0 || y>=96)return;
  int o=(y*96+x)*4;s->pixels[o]=r;s->pixels[o+1]=g;s->pixels[o+2]=b;s->pixels[o+3]=255;
}
static void update_hall(chip_state_t *s) {
  bool active=s->position<(int)s->hall_width;
  pin_mode(s->hall,active?OUTPUT:INPUT);
  if(active)pin_write(s->hall,0);
}
static void draw(chip_state_t *s) {
  for(int y=0;y<96;y++)for(int x=0;x<96;x++)pixel(s,x,y,12,20,30);
  for(int i=0;i<4;i++)for(int y=5;y<17;y++)for(int x=5+i*23;x<23+i*23;x++)
    pixel(s,x,y,(s->mask&(1<<i))?35:35,(s->mask&(1<<i))?230:45,65);
  for(int y=25;y<87;y++)for(int x=17;x<79;x++) {
    int d=(x-48)*(x-48)+(y-56)*(y-56);
    if(d>27*27 && d<30*30)pixel(s,x,y,90,120,150);
  }
  double a=s->position*(6.283185307179586/(double)s->revolution)-1.570796326794897;
  for(int r=0;r<28;r++)pixel(s,48+(int)(cos(a)*r),56+(int)(sin(a)*r),255,210,65);
  // Bottom status bar: green = legal sequence, red = at least one skipped/invalid phase.
  for(int y=91;y<95;y++)for(int x=4;x<92;x++)pixel(s,x,y,s->errors?245:40,s->errors?40:180,55);
  buffer_write(s->framebuffer,0,s->pixels,sizeof(s->pixels));
}
static void sample(void *data) {
  chip_state_t *s=data;
  uint8_t mask=0;
  if(pin_read(s->common))for(int i=0;i<4;i++)mask|=pin_read(s->in[i])<<i;
  if(mask==s->mask)return;
  s->mask=mask;
  if(mask==0) { draw(s);return; } // Rotor retains position and last phase when released.
  int phase=-1;
  for(int i=0;i<8;i++)if(sequence[i]==mask)phase=i;
  if(phase<0) {s->errors++;s->phase=-1;printf("U%u invalid coil mask=0x%x\n",s->unit,mask);}
  else {
    if(s->phase>=0) {
      int delta=(phase-s->phase+8)%8;
      if(delta==1)s->position=(s->position+1)%s->revolution;
      else if(delta==7)s->position=(s->position+s->revolution-1)%s->revolution;
      else if(delta!=0) {s->errors++;printf("U%u skipped phase %d -> %d\n",s->unit,s->phase,phase);}
    }
    s->phase=phase;
  }
  update_hall(s);draw(s);
}
static void input_change(void *data,pin_t pin,uint32_t value) {
  (void)pin;(void)value;
  // Collapse same-register GPIO update callbacks into one settled four-bit sample.
  chip_state_t *s=data;timer_start(s->settle,1,false);
}
void chip_init(void) {
  chip_state_t *s=calloc(1,sizeof(*s));
  s->phase=-1;s->unit=attr_read(attr_init("unit",0));
  s->revolution=attr_read(attr_init("revolution",4096));
  if(s->revolution<1000 || s->revolution>20000)s->revolution=4096;
  s->hall_width=attr_read(attr_init("hallWidth",32));
  if(s->hall_width<1 || s->hall_width>=s->revolution)s->hall_width=32;
  s->position=attr_read(attr_init("position",0))%s->revolution;
  s->hall=pin_init("HALL",INPUT);update_hall(s);
  uint32_t width,height;s->framebuffer=framebuffer_init(&width,&height);
  timer_config_t timer={.callback=sample,.user_data=s};s->settle=timer_init(&timer);
  pin_watch_config_t watch={.edge=BOTH,.pin_change=input_change,.user_data=s};
  char name[4];
  for(int i=0;i<4;i++) {snprintf(name,sizeof(name),"IN%d",i+1);s->in[i]=pin_init(name,INPUT_PULLDOWN);pin_watch(s->in[i],&watch);}
  s->common=pin_init("COMMON_12V",INPUT_PULLDOWN);pin_watch(s->common,&watch);
  pin_init("GND",INPUT);draw(s);
}
