// Minimal host API double for the project's rotor model, not a Wokwi implementation.
#ifndef WOKWI_TEST_API_H
#define WOKWI_TEST_API_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int pin_t;
typedef int timer_t;
typedef int buffer_t;
enum {INPUT,OUTPUT,INPUT_PULLDOWN,BOTH};
typedef struct {void(*callback)(void*);void*user_data;} timer_config_t;
typedef struct {int edge;void(*pin_change)(void*,pin_t,uint32_t);void*user_data;} pin_watch_config_t;
extern int values[32],modes[32];
static inline int pin_read(pin_t p){return values[p];}
static inline void pin_mode(pin_t p,int mode){modes[p]=mode;}
static inline void pin_write(pin_t p,int value){values[p]=value;}
static inline pin_t pin_init(const char*name,int mode){(void)name;(void)mode;return 0;}
static inline void pin_watch(pin_t p,const pin_watch_config_t*w){(void)p;(void)w;}
static inline void timer_start(timer_t t,int us,bool repeat){(void)t;(void)us;(void)repeat;}
static inline timer_t timer_init(const timer_config_t*c){(void)c;return 0;}
static inline uint32_t attr_init(const char*n,uint32_t d){(void)n;return d;}
static inline uint32_t attr_read(uint32_t a){return a;}
static inline buffer_t framebuffer_init(uint32_t*w,uint32_t*h){*w=*h=96;return 0;}
static inline void buffer_write(buffer_t b,int o,const void*p,size_t n){(void)b;(void)o;(void)p;(void)n;}
#endif
