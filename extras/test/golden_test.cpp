#include "Arduino.h"
#include "SPI.h"
#include "ADRF6655.h"
std::vector<uint32_t> g_words; uint8_t g_le=5; int g_leState=0; uint32_t g_cur=0; int g_nb=0;
SerialMock Serial; SPIMock SPI;
static void dump(const char*tag){ printf("%s:",tag); for(auto w:g_words) printf(" %06X",w); printf("\n"); g_words.clear(); }
int main(){
  ADRF6655 d(5);
  d.begin(20.0); dump("begin");
  double fs[]={1050,1100.0,1200.5,1529.98,1530,1530.02,1700,1740,1740.5,1740.02,1800.04,1999.98,2100.16,2299.98,2300};
  for(double f:fs){ bool ok=d.setFrequency(f); char t[64]; snprintf(t,64,"f=%.3f ok=%d",f,ok); dump(t);
  }
  // manual
  d.setFrequencyManual(false,87,1000,500); dump("manual frac");
  d.setFrequencyManual(true,90,1000,0); dump("manual int");
  d.setFrequencyManual(true,10,1000,0); dump("manual bad int");
  // R configs (valid defaults used by examples)
  d.configureR3_Dither(true,7,1); dump("r3");
  d.configureR4_ChargePumpPFD(MUXOUT_LOCK_DETECT,REF_PATH_1X,false,true,4,2,false,CP_BOTH_ON,true,true,ABD_0ns); dump("r4");
  d.configureR5_LOPathMixer(true,15,true,LO_DIV_BY_2,false,false); dump("r5");
  printf("freq=%.4f\n", d.getFrequency());
  return 0;
}
