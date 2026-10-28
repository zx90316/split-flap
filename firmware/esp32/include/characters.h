// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace splitflap {
// Physical order: JonnyBooker/split-flap Unit/Unit.ino at 0c6ef692,
// identical to the retained Instructions/SplitFlapInstructions.pdf, page 14.
// Default uses umlaut flaps. Set noUmlauts for the alternative $, &, # flaps.
inline bool parseText(const char* text, uint8_t* indices, size_t count, bool noUmlauts=false) {
  size_t n=0;
  while (*text) {
    if(n>=count)return false;
    const unsigned char c=static_cast<unsigned char>(*text++);
    uint8_t index=255;
    if(c==' ')index=0;
    else if(c>='A' && c<='Z')index=c-'A'+1;
    else if(c>='0' && c<='9')index=c-'0'+30;
    else if(c==':')index=40;
    else if(c=='.')index=41;
    else if(c=='-')index=42;
    else if(c=='?')index=43;
    else if(c=='!')index=44;
    else if(noUmlauts && c=='$')index=27;
    else if(noUmlauts && c=='&')index=28;
    else if(noUmlauts && c=='#')index=29;
    else if(!noUmlauts && c==0xc3) {
      const unsigned char next=static_cast<unsigned char>(*text);
      if(next==0x84)index=27;else if(next==0x96)index=28;else if(next==0x9c)index=29;
      else return false;
      ++text;
    }
    if(index==255)return false;
    indices[n++]=index;
  }
  while(n<count)indices[n++]=0;
  return true;
}
}
