#include "Arduino.h"
#include "SPI.h"
#include "ADRF6655.h"
std::vector<uint32_t> g_words; uint8_t g_le=5; int g_leState=0; uint32_t g_cur=0; int g_nb=0;
SerialMock Serial; SPIMock SPI;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(){
  { ADRF6655 d(5); d.begin(20.0,-1,-1,false);
    CHECK(g_words.size()==5);                       // R7 R6 R5 R4 R3 only
    CHECK(((d.getShadowReg(5)>>6)&1)==0);           // PLL still off
    g_words.clear();
    CHECK(d.setFrequency(1740.0));
    CHECK(((d.getShadowReg(5)>>6)&1)==1);
    CHECK(d.getFrequencyHz()==1740000000ULL); CHECK(d.getLastFrequencyErrorHz()==0); }
  { ADRF6655 d(5); d.begin(20.0);
    // exactness via Hz API
    CHECK(d.setFrequencyHz(1740020000ULL)); CHECK(d.getFrequencyHz()==1740020000ULL);
    // off-grid request: fixed MOD=1000 gives 20 kHz grid -> reports error
    CHECK(d.setFrequencyHz(1740012345ULL));
    printf("fixed MOD err=%d Hz, achieved=%llu\n",(int)d.getLastFrequencyErrorHz(),(unsigned long long)d.getFrequencyHz());
    CHECK(llabs(d.getLastFrequencyErrorHz())<=10000);
    int32_t fixedErr=llabs(d.getLastFrequencyErrorHz());
    d.setAutoModulus(true);
    CHECK(d.setFrequencyHz(1740012345ULL));
    printf("auto MOD  err=%d Hz, achieved=%llu\n",(int)d.getLastFrequencyErrorHz(),(unsigned long long)d.getFrequencyHz());
    CHECK(llabs(d.getLastFrequencyErrorHz())<=fixedErr);
    CHECK(llabs(d.getLastFrequencyErrorHz())<=20000000/(2*2047)+1);
    // achieved == shadow-derived value, error consistent
    CHECK((int64_t)d.getFrequencyHz()-1740012345LL==d.getLastFrequencyErrorHz());
    d.setAutoModulus(false);
    CHECK(d.setModulus(2000)); CHECK(!d.setModulus(0)); CHECK(!d.setModulus(2048));
    CHECK(d.setFrequencyHz(1740010000ULL)); CHECK(d.getFrequencyHz()==1740010000ULL);
    // rounding carry: just under an integer boundary
    CHECK(d.setModulus(1000)); CHECK(d.setFrequencyHz(1759999999ULL));
    CHECK(d.getFrequencyHz()==1760000000ULL);
    // out of range rejected, nothing written
    g_words.clear();
    CHECK(!d.setFrequency(1049.9)); CHECK(!d.setFrequency(2300.1)); CHECK(!d.setFrequency(-5)); CHECK(!d.setFrequency(NAN));
    CHECK(!d.setFrequencyHz(0)); CHECK(g_words.empty()); }
  { // uint8 wrap regression: PFD = 5 MHz -> N up to 460
    ADRF6655 d(5); d.begin(20.0);
    d.configureR4_ChargePumpPFD(MUXOUT_LOCK_DETECT,REF_PATH_QUARTER,false,true,4,2,false,CP_BOTH_ON,true,true,ABD_0ns);
    CHECK(d.getPFDFrequencyMHz()==5.0);
    g_words.clear();
    CHECK(!d.setFrequency(1740.0));   // INT=348 -> old code wrapped to 92 and "succeeded"
    CHECK(g_words.empty()); }
  { // validation: nothing written on bad args
    ADRF6655 d(5); d.begin(20.0); g_words.clear();
    CHECK(!d.configureR0_IntegerDivide(false,20)); CHECK(!d.configureR0_IntegerDivide(true,124));
    CHECK(!d.configureR1_Modulus(0)); CHECK(!d.configureR1_Modulus(2048)); CHECK(!d.configureR2_Fractional(2048));
    CHECK(!d.configureR3_Dither(true,5,1)); CHECK(!d.configureR3_Dither(true,1,0x20000));
    CHECK(!d.configureR4_ChargePumpPFD(5,1,false,true,4,2,false,0,true,true,0));
    CHECK(!d.configureR4_ChargePumpPFD(0,4,false,true,4,2,false,0,true,true,0));
    CHECK(!d.configureR5_LOPathMixer(true,16,true,LO_DIV_BY_2,false,false));
    CHECK(!d.configureR6_VCOControl(true,true,true,true,false,64,false,0));
    CHECK(!d.configureR6_VCOControl(true,true,true,true,false,47,false,64));
    CHECK(!d.setFrequencyManual(true,90,0,0)); CHECK(!d.setFrequencyManual(false,87,1000,1000));
    CHECK(g_words.empty());
    CHECK(d.configureR7_ExternalVCO(false)); }
  { ADRF6655 d(5); d.begin(20.0); Serial.quiet=false; d.printStatus(); Serial.quiet=true; }
  printf(fails?"%d FAILURES\n":"ALL PASS\n",fails); return fails!=0;
}
