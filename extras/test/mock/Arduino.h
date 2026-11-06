#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <iostream>
#define F(x) (x)
class String;
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
#define MSBFIRST 1
#define SPI_MODE0 0
#define ESP32 1
extern std::vector<uint32_t> g_words;
extern uint8_t g_le; extern int g_leState;
extern uint32_t g_cur; extern int g_nb;
inline void pinMode(uint8_t,uint8_t){}
inline void digitalWrite(uint8_t p,uint8_t v){ if(p==g_le){ if(v==HIGH&&g_leState==LOW){ g_words.push_back(g_cur); g_cur=0;g_nb=0;} g_leState=v; } }
inline int digitalRead(uint8_t){return HIGH;}
inline void delay(unsigned long){}
inline void delayMicroseconds(unsigned int){}
struct SerialMock{
  bool quiet=true;
  void begin(long){}
  void print(const char*s){ if(!quiet) fputs(s,stdout);}
  void println(const char*s){ if(!quiet){puts(s);} }
  void println(){ if(!quiet) puts("");}
  void print(double v,int d=2){ if(!quiet) printf("%.*f",d,v);}  
  void println(double v,int d=2){ if(!quiet) printf("%.*f\n",d,v);}  
  void print(long v){ if(!quiet) printf("%ld",v);} void println(long v){ if(!quiet) printf("%ld\n",v);} 
  void print(unsigned long v){ if(!quiet) printf("%lu",v);} void println(unsigned long v){ if(!quiet) printf("%lu\n",v);} 
  void print(int v){ if(!quiet) printf("%d",v);} void println(int v){ if(!quiet) printf("%d\n",v);} 
  void print(unsigned v){ if(!quiet) printf("%u",v);} void println(unsigned v){ if(!quiet) printf("%u\n",v);} 
  void print(const String&v); void println(const String&v);
  int available(){ return 0; } int read(){ return -1; }
  explicit operator bool() const { return true; }
  void print(unsigned char v){ if(!quiet) printf("%u",v);} void println(unsigned char v){ if(!quiet) printf("%u\n",v);} 
};
extern SerialMock Serial;

#include "Arduino_ext.h"
