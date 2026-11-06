#pragma once
#include "Arduino.h"
struct SPISettings{ SPISettings(uint32_t,int,int){} };
struct SPIMock{
  void begin(){} void begin(int,int,int,int){}
  void beginTransaction(SPISettings){} void endTransaction(){}
  void transfer(uint8_t b){ g_cur=(g_cur<<8)|b; g_nb++; }
};
extern SPIMock SPI;
