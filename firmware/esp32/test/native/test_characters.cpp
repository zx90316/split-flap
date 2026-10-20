// SPDX-License-Identifier: GPL-3.0-only
#include "characters.h"
#include <cassert>
#include <iostream>
int main(){uint8_t x[10]={};
  assert(splitflap::parseText("AZÄÖÜ09:?!",x,10));
  const uint8_t expected[10]={1,26,27,28,29,30,39,40,43,44};
  for(int i=0;i<10;i++)assert(x[i]==expected[i]);
  assert(splitflap::parseText("$&#",x,10,true));assert(x[0]==27&&x[1]==28&&x[2]==29&&x[9]==0);
  assert(!splitflap::parseText("$",x,10));assert(!splitflap::parseText("Ä",x,10,true));
  assert(!splitflap::parseText("a",x,10));assert(!splitflap::parseText("AB",x,1));
  assert(!splitflap::parseText("\xc3",x,10));assert(!splitflap::parseText("你好",x,10));
  assert(splitflap::parseText("",x,10));for(int i=0;i<10;i++)assert(x[i]==0);
  std::cout<<"Character order, UTF-8, alternative flaps, padding and rejection tests passed\n";
}
