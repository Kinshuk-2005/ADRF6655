/*
 * ADRF6655.cpp
 *
 * Bit positions used in every bitmask assembly are listed with the
 * field name and their DB-number so they can be cross-checked directly
 * against the datasheet register diagrams.
 *
 * Verified positions (resolved against prose p.31-32 and Fig.69):
 *   R5: CDAC=DB11:DB8, MBE=DB7, PLEN=DB6, LDIV=DB5, LXL=DB4, LDRV=DB3
 *   R6: reserved=DB23:DB21, CPEN=DB20, L3EN=DB19, LVEN=DB18,
 *       VCOEN=DB17, VCOSW=DB16, VC[5:0]=DB15:DB10,
 *       VBSRC=DB9, VBS[5:0]=DB8:DB3
 *   R1/R2: 11-bit fields at DB13:DB3  (shift left by 3)
 */

#include "ADRF6655.h"

// ─────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────

ADRF6655::ADRF6655(uint8_t lePin, uint32_t spiSpeed)
    : _lePin(lePin),
      _spiSpeed(spiSpeed),
      _refHz(20000000UL),
      _pfdHz(20000000UL),
      _mod(DEFAULT_MOD),
      _autoMod(false),
      _lastErrHz(0),
      _lockWaitMs(20)
{
    memset(_shadow, 0, sizeof(_shadow));
}

// ─────────────────────────────────────────────────────────────────
//  begin
// ─────────────────────────────────────────────────────────────────

void ADRF6655::begin(double refFreqMHz, int sckPin, int mosiPin, bool autoStart)
{
    _refHz = (uint32_t)(refFreqMHz * 1e6 + 0.5);
    _pfdHz = _refHz;        // default 1:1; R4 config updates this

    pinMode(_lePin, OUTPUT);
    digitalWrite(_lePin, LOW);  // LE idles LOW

    // On ESP32 SPI.begin(sck, miso, mosi, ss); MISO unused, ss=-1 (manual LE)
#if defined(ESP32)
    if (sckPin >= 0 && mosiPin >= 0) {
        SPI.begin(sckPin, /*miso=*/-1, mosiPin, /*ss=*/-1);
    } else {
        SPI.begin(18, -1, 23, -1);   // VSPI defaults
    }
#else
    (void)sckPin; (void)mosiPin;
    SPI.begin();
#endif

    powerOnSequence(autoStart);
}

// ─────────────────────────────────────────────────────────────────
//  _spiSend  (private)
//
//  SPI timing requirements from datasheet Table 2:
//    t2  DATA→CLK setup  ≥ 10 ns   (met by SPI peripheral)
//    t3  DATA→CLK hold   ≥ 10 ns   (met by SPI peripheral)
//    t4  CLK high        ≥ 25 ns   (met at ≤ 20 MHz)
//    t5  CLK low         ≥ 25 ns   (met at ≤ 20 MHz)
//    t6  CLK→LE setup    ≥ 10 ns   (delayMicroseconds(1) = 1000 ns)
//    t1  LE setup        ≥ 20 ns   (covered by same delay)
//    t7  LE pulse width  ≥ 20 ns   (delayMicroseconds(1) = 1000 ns)
// ─────────────────────────────────────────────────────────────────

void ADRF6655::_spiSend(uint32_t word24)
{
    SPI.beginTransaction(SPISettings(_spiSpeed, MSBFIRST, SPI_MODE0));

    // Three bytes, MSB first: DB23..DB16, DB15..DB8, DB7..DB0
    SPI.transfer((word24 >> 16) & 0xFF);
    SPI.transfer((word24 >>  8) & 0xFF);
    SPI.transfer( word24        & 0xFF);

    delayMicroseconds(1);          // t6: CLK-to-LE setup
    digitalWrite(_lePin, HIGH);    // latch rising edge
    delayMicroseconds(1);          // t7: LE pulse width
    digitalWrite(_lePin, LOW);     // return to idle

    SPI.endTransaction();
}

// ─────────────────────────────────────────────────────────────────
//  writeRegister  (public low-level primitive)
// ─────────────────────────────────────────────────────────────────

void ADRF6655::writeRegister(uint8_t address, uint32_t bitmask)
{
    // Force DB2:DB0 to the correct register address
    uint32_t word = (bitmask & 0xFFFFF8UL) | ((uint32_t)(address & 0x07));
    _shadow[address & 0x07] = word;
    _spiSend(word);
}

// ─────────────────────────────────────────────────────────────────
//  R0 – Integer Divide Control
//
//  DB10     DM       divide mode: 0=fractional, 1=integer
//  DB9:DB3  ID6:ID0  7-bit INT  (shift left 3)
//  DB2:DB0           address 000  (inserted by writeRegister)
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR0_IntegerDivide(bool integerMode, uint8_t integerDivRatio)
{
    // Reject INT outside the valid range for the selected mode
    if (integerMode) {
        if (integerDivRatio < INT_INT_MODE_MIN  || integerDivRatio > INT_INT_MODE_MAX)  return false;
    } else {
        if (integerDivRatio < INT_FRAC_MODE_MIN || integerDivRatio > INT_FRAC_MODE_MAX) return false;
    }

    uint32_t bitmask = 0;
    if (integerMode)
        bitmask |= (1UL << 10);                           // DB10: DM=1

    bitmask |= ((uint32_t)(integerDivRatio & 0x7F) << 3); // DB9:DB3: ID6:ID0

    writeRegister(0, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  R1 – Modulus Divide Control
//
//  DB13:DB3  MD10:MD0  11-bit MOD  (shift left 3)
//  DB2:DB0             address 001
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR1_Modulus(uint16_t modulusValue)
{
    if (modulusValue < MOD_MIN || modulusValue > MOD_MAX) return false;

    // DB13:DB3 – 11-bit MOD
    uint32_t bitmask = (uint32_t)(modulusValue & 0x7FF) << 3;
    writeRegister(1, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  R2 – Fractional Divide Control
//
//  DB13:DB3  FD10:FD0  11-bit FRAC  (shift left 3)
//  DB2:DB0             address 010
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR2_Fractional(uint16_t fractionalValue)
{
    if (fractionalValue > 0x7FF) return false;  // 11-bit field

    // DB13:DB3 – 11-bit FRAC
    uint32_t bitmask = (uint32_t)(fractionalValue & 0x7FF) << 3;
    writeRegister(2, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  R3 – Σ-Δ Modulator Dither Control
//
//  DB22:DB21  DITH1:DITH0  magnitude encoding
//  DB20       DEN           dither enable
//  DB19:DB3   DV16:DV0     17-bit restart value  (shift left 3)
//  DB2:DB0                 address 011
//
//  Magnitude encoding (datasheet Fig.73):
//    magnitude 15 → DITH1:DITH0 = 00
//    magnitude  7 → DITH1:DITH0 = 01
//    magnitude  3 → DITH1:DITH0 = 10
//    magnitude  1 → DITH1:DITH0 = 11  (recommended when enabled)
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR3_Dither(bool     ditherEnable,
                                   uint8_t  ditherMagnitude,
                                   uint32_t ditherRestartValue)
{
    // Magnitude must be exactly one of the four encodable values.
    uint8_t dithBits;
    switch (ditherMagnitude) {
        case 15: dithBits = 0b00; break;
        case  7: dithBits = 0b01; break;
        case  3: dithBits = 0b10; break;
        case  1: dithBits = 0b11; break;
        default: return false;
    }

    if (ditherRestartValue > 0x1FFFFUL) return false;   // 17-bit field

    uint32_t bitmask = 0;
    bitmask |= ((uint32_t)(dithBits & 0x03) << 21);           // DB22:DB21 – DITH
    if (ditherEnable) bitmask |= (1UL << 20);                  // DB20 – DEN
    bitmask |= ((ditherRestartValue & 0x1FFFFUL) << 3);        // DB19:DB3 – DV16:DV0

    writeRegister(3, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  R4 – Charge Pump, PFD, and Reference Path Control
//
//  DB23:DB21  RMS2:RMS0   MUXOUT source (3 bits)
//  DB20:DB19  RS1:RS0     reference path scaling (2 bits)
//  DB18       CPM         CP ref source
//  DB17       CPBD        phase-offset polarity
//  DB16:DB12  CPB4:CPB0   phase-offset multiplier (5 bits)
//  DB11:DB10  CPP1:CPP0   CP current multiplier (2 bits)
//  DB9        CPS         CP control source
//  DB8:DB7    CPC1:CPC0   forced CP state (2 bits)
//  DB6        PE1         divider-path edge
//  DB5        PE0         reference-path edge
//  DB4:DB3    PAB1:PAB0   antibacklash delay (2 bits)
//  DB2:DB0                address 100
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR4_ChargePumpPFD(uint8_t muxoutSource,
                                          uint8_t refPathSource,
                                          bool    useExternalRset,
                                          bool    phaseOffsetPositive,
                                          uint8_t phaseOffsetMultiplier,
                                          uint8_t cpCurrentMultiplier,
                                          bool    forceChargePumpState,
                                          uint8_t cpForceState,
                                          bool    dividerEdgeFalling,
                                          bool    refEdgeFalling,
                                          uint8_t antibacklashDelay)
{
    // Input validation (nothing is written if any argument is invalid)
    if (muxoutSource > 7 || muxoutSource == 5 || muxoutSource == 6) return false; // 5,6 reserved
    if (refPathSource > 3)           return false;
    if (phaseOffsetMultiplier > 31)  return false;
    if (cpCurrentMultiplier < 1 || cpCurrentMultiplier > 4) return false;
    if (cpForceState > 3)            return false;
    if (antibacklashDelay > 3)       return false;

    // CPP encoding: multiplier 1→00, 2→01, 3→10, 4→11
    uint8_t cppBits = (cpCurrentMultiplier - 1) & 0x03;

    uint32_t bitmask = 0;
    bitmask |= ((uint32_t)(muxoutSource & 0x07) << 21);        // DB23:DB21 – RMS
    bitmask |= ((uint32_t)(refPathSource & 0x03) << 19);       // DB20:DB19 – RS
    if (useExternalRset)       bitmask |= (1UL << 18);          // DB18 – CPM
    if (phaseOffsetPositive)   bitmask |= (1UL << 17);          // DB17 – CPBD
    bitmask |= ((uint32_t)(phaseOffsetMultiplier & 0x1F) << 12);// DB16:DB12 – CPB
    bitmask |= ((uint32_t)(cppBits & 0x03) << 10);             // DB11:DB10 – CPP
    if (forceChargePumpState)  bitmask |= (1UL << 9);           // DB9 – CPS
    bitmask |= ((uint32_t)(cpForceState & 0x03) << 7);          // DB8:DB7 – CPC
    if (!dividerEdgeFalling)   bitmask |= (1UL << 6);           // DB6 – PE1 (0=falling)
    if (!refEdgeFalling)       bitmask |= (1UL << 5);           // DB5 – PE0
    bitmask |= ((uint32_t)(antibacklashDelay & 0x03) << 3);     // DB4:DB3 – PAB

    // Update _pfdHz so setFrequency() calculations stay correct
    switch (refPathSource) {
        case REF_PATH_2X:      _pfdHz = _refHz * 2UL; break;
        case REF_PATH_1X:      _pfdHz = _refHz;       break;
        case REF_PATH_HALF:    _pfdHz = _refHz / 2UL; break;
        case REF_PATH_QUARTER: _pfdHz = _refHz / 4UL; break;
    }

    writeRegister(4, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  R5 – LO Path and Mixer Control
//
//  Bit positions verified against datasheet prose (p.31):
//    "mixer bias enable address (DB7)" and "PLL enable address (DB6)"
//
//  DB11:DB8  CDAC3:CDAC0  distortion compensation (4 bits)
//  DB7       MBE          mixer bias enable
//  DB6       PLEN         PLL enable
//  DB5       LDIV         LO divider: 0=÷3, 1=÷2
//  DB4       LXL          LO path: 0=internal output, 1=external input
//  DB3       LDRV         LO output buffer
//  DB2:DB0               address 101
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR5_LOPathMixer(bool    mixerBiasEnable,
                                        uint8_t cdacValue,
                                        bool    pllEnable,
                                        uint8_t loDivider,
                                        bool    loExternal,
                                        bool    loOutputDriverOn)
{
    if (cdacValue > 15) return false;
    if (loDivider != LO_DIV_BY_2 && loDivider != LO_DIV_BY_3) return false;

    uint32_t bitmask = 0;
    bitmask |= ((uint32_t)(cdacValue & 0x0F) << 8);  // DB11:DB8 – CDAC
    if (mixerBiasEnable)    bitmask |= (1UL << 7);    // DB7  – MBE
    if (pllEnable)          bitmask |= (1UL << 6);    // DB6  – PLEN
    if (loDivider == LO_DIV_BY_2)
                            bitmask |= (1UL << 5);    // DB5  – LDIV (1=÷2)
    if (loExternal)         bitmask |= (1UL << 4);    // DB4  – LXL
    if (loOutputDriverOn)   bitmask |= (1UL << 3);    // DB3  – LDRV

    writeRegister(5, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  R6 – VCO Control and PLL Enables
//
//  Bit positions verified against Fig.69 master table:
//
//  DB23:DB21  reserved (write 0)
//  DB20       CPEN     charge pump enable
//  DB19       L3EN     3.3 V LDO enable
//  DB18       LVEN     VCO LDO enable
//  DB17       VCOEN    internal VCO enable
//  DB16       VCOSW    VCO switch
//  DB15:DB10  VC5:VC0  VCO amplitude (6 bits)
//  DB9        VBSRC    band source
//  DB8:DB3    VBS5:VBS0  VCO band (6 bits)
//  DB2:DB0              address 110
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR6_VCOControl(bool    chargePumpEnable,
                                       bool    ldo3v3Enable,
                                       bool    vcoLdoEnable,
                                       bool    vcoEnable,
                                       bool    vcoSwitchManual,
                                       uint8_t vcoAmplitude,
                                       bool    vcoBandFromSPI,
                                       uint8_t vcoBandSelect)
{
    if (vcoAmplitude > 63 || vcoBandSelect > 63) return false;   // 6-bit fields

    uint32_t bitmask = 0;
    if (chargePumpEnable)  bitmask |= (1UL << 20);                   // DB20 – CPEN
    if (ldo3v3Enable)      bitmask |= (1UL << 19);                   // DB19 – L3EN
    if (vcoLdoEnable)      bitmask |= (1UL << 18);                   // DB18 – LVEN
    if (vcoEnable)         bitmask |= (1UL << 17);                   // DB17 – VCOEN
    if (vcoSwitchManual)   bitmask |= (1UL << 16);                   // DB16 – VCOSW
    bitmask |= ((uint32_t)(vcoAmplitude & 0x3F) << 10);              // DB15:DB10 – VC5:VC0
    if (vcoBandFromSPI)    bitmask |= (1UL << 9);                    // DB9  – VBSRC
    bitmask |= ((uint32_t)(vcoBandSelect & 0x3F) << 3);              // DB8:DB3  – VBS5:VBS0

    writeRegister(6, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  R7 – External VCO Control
//
//  DB22  XVCO  0=internal VCO, 1=external VCO at LOP/LON
//  DB2:DB0     address 111
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::configureR7_ExternalVCO(bool useExternalVCO)
{
    uint32_t bitmask = 0;
    if (useExternalVCO) bitmask |= (1UL << 22);   // DB22 – XVCO

    writeRegister(7, bitmask);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  deactivate / activate / deep-standby helpers
// ─────────────────────────────────────────────────────────────────

void ADRF6655::deactivate()
{
    // Clear PLEN (DB6) in R5, preserve everything else
    writeRegister(5, _shadow[5] & ~(1UL << 6));
}

void ADRF6655::activate()
{
    // Set PLEN (DB6) in R5, preserve everything else
    writeRegister(5, _shadow[5] | (1UL << 6));
    delay(100);   // wait >100 ms for supply lines to stabilise
}

void ADRF6655::enterDeepStandby()
{
    deactivate();   // PLEN=0 first
    // Disable CP, both LDOs, and internal VCO
    configureR6_VCOControl(
        /*CP=*/     false,
        /*3v3=*/    false,
        /*vcoLDO=*/ false,
        /*VCO=*/    false,
        /*manual=*/ false,
        /*amp=*/    47,
        /*bandSPI=*/false,
        /*band=*/   0
    );
}

void ADRF6655::wakeFromStandby()
{
    // Restore R6 to normal operating state; leaves PLL disabled
    configureR6_VCOControl(
        /*CP=*/     true,
        /*3v3=*/    true,
        /*vcoLDO=*/ true,
        /*VCO=*/    true,
        /*manual=*/ false,   // auto band calibration
        /*amp=*/    47,
        /*bandSPI=*/false,
        /*band=*/   0
    );
    // Caller must now call setFrequency() to retune and lock
}

// ─────────────────────────────────────────────────────────────────
//  powerOnSequence
// ─────────────────────────────────────────────────────────────────

void ADRF6655::powerOnSequence(bool loadPlaceholder)
{
    _baseBlockInit();

    // ── Phase 3: Frequency Load & Activation ─────────────────────
    // Placeholder: INT=87, integer mode → fLO = 87 × 20 MHz = 1740 MHz
    // Call setFrequency() after begin() to go to your actual target.
    // Skipped when begin(..., autoStart=false): the PLL then stays off
    // (PLEN=0) until the first setFrequency().
    if (loadPlaceholder)
        _phaseThreeSequence(/*integerMode=*/true, /*INT=*/87,
                            /*MOD=*/DEFAULT_MOD, /*FRAC=*/0);
}

// Phase 2: write the static configuration registers with the PLL dark.
void ADRF6655::_baseBlockInit()
{

    // R7: use internal VCO
    configureR7_ExternalVCO(false);

    // R6: all blocks on, VCO amplitude 47 (recommended), auto band cal
    configureR6_VCOControl(
        /*CP=*/     true,
        /*3v3=*/    true,
        /*vcoLDO=*/ true,
        /*VCO=*/    true,
        /*manual=*/ false,
        /*amp=*/    47,
        /*bandSPI=*/false,
        /*band=*/   0
    );

    // R5: PLEN=0 (PLL off), mixer on, CDAC=15, ÷2 mode, internal LO
    configureR5_LOPathMixer(
        /*MBE=*/    true,
        /*CDAC=*/   15,
        /*PLEN=*/   false,   // ← PLL disabled during Phase 2
        /*LDIV=*/   LO_DIV_BY_2,
        /*ext=*/    false,
        /*driver=*/ false
    );

    // R4: lock detect, 1× REFIN, internal CP ref, positive offset,
    //     mult=4 (recommended), 500 µA (2×), normal PFD control,
    //     falling edges, 0 ns antibacklash
    configureR4_ChargePumpPFD(
        MUXOUT_LOCK_DETECT, REF_PATH_1X,
        /*extRset=*/    false,
        /*posOffset=*/  true,
        /*offMult=*/    4,
        /*cpMult=*/     2,
        /*forceCP=*/    false,
        CP_BOTH_ON,
        /*divFall=*/    true,
        /*refFall=*/    true,
        ABD_0ns
    );

    // R3: dither disabled (recommended), restart value=1
    configureR3_Dither(false, 1, 1);
}

// ─────────────────────────────────────────────────────────────────
//  _phaseThreeSequence  (private)
//
//  Phase 3 sequence exactly as specified:
//    R2 → R1 → wait >500 µs → R5(PLEN=1) → wait >100 ms → R0
//
//  Caller must have:
//    (a) Called deactivate() so _shadow[5] already has PLEN=0.
//    (b) Updated the LDIV bit in _shadow[5] if the divider changed.
// ─────────────────────────────────────────────────────────────────

void ADRF6655::_phaseThreeSequence(bool     integerMode,
                                    uint8_t  intVal,
                                    uint16_t modVal,
                                    uint16_t fracVal)
{
    // Step 1: R2 – FRAC (written first in the sequence)
    configureR2_Fractional(fracVal);

    // Step 2: R1 – MOD
    configureR1_Modulus(modVal);

    // Step 3: Critical delay > 500 µs
    // Gives the digital core time to queue initial integer boundary
    // values before the VCO autocalibration engine activates.
    delayMicroseconds(600);

    // Step 4+5: R5 PLEN=1, then wait > 100 ms
    // activate() sets PLEN=1 in R5 (preserving all other fields from
    // the shadow, including the LDIV bit already set by the caller)
    // and then waits 100 ms for CP and loop-filter supply stabilisation.
    activate();

    // Step 6: R0 – INT (the VCO autocalibration trigger)
    configureR0_IntegerDivide(integerMode, intVal);

    // Step 7: wait for PLL to lock
    delay(_lockWaitMs);
}

// ─────────────────────────────────────────────────────────────────
//  setFrequency
// ─────────────────────────────────────────────────────────────────

// Fractional word for a remainder: round(remHz * mod / pfdHz).
uint16_t ADRF6655::_fracFor(uint32_t remHz, uint32_t pfdHz, uint16_t mod)
{
    return (uint16_t)(((uint64_t)remHz * mod + (pfdHz >> 1)) / pfdHz);
}

// Search MOD = 1..2047 for the smallest |error|; first (smallest) MOD wins ties.
void ADRF6655::_bestMod(uint32_t remHz, uint32_t pfdHz,
                        uint16_t &mod, uint16_t &frac)
{
    uint32_t bestErr = 0xFFFFFFFFUL;
    uint16_t bestMod = 1, bestFrac = 0;
    for (uint16_t m = 1; m <= MOD_MAX; m++) {
        uint16_t f = _fracFor(remHz, pfdHz, m);
        uint64_t approx = ((uint64_t)pfdHz * f + (m >> 1)) / m;   // Hz above INT
        uint32_t err = (approx > remHz) ? (uint32_t)(approx - remHz)
                                        : (uint32_t)(remHz - approx);
        if (err < bestErr) { bestErr = err; bestMod = m; bestFrac = f; }
        if (err == 0) break;
    }
    mod = bestMod;
    frac = bestFrac;
}

bool ADRF6655::setFrequency(double loFreqMHz)
{
    // Range check in floating point first so negatives/NaN never reach the
    // unsigned conversion; the exact check is repeated on the integer value.
    if (!(loFreqMHz >= LO_FREQ_MIN_MHZ && loFreqMHz <= LO_FREQ_MAX_MHZ)) {
        Serial.print(F("[ADRF6655] setFrequency out of range: "));
        Serial.print(loFreqMHz, 3);
        Serial.println(F(" MHz  (valid: 1050-2300 MHz)"));
        return false;
    }
    // NOTE: on AVR `double` is 32-bit, so this conversion is good to ~100 Hz.
    // Use setFrequencyHz() when you need an exact Hz value on AVR.
    return setFrequencyHz((uint64_t)(loFreqMHz * 1e6 + 0.5));
}

bool ADRF6655::setFrequencyHz(uint64_t loHz)
{
    // ── Range check ───────────────────────────────────────────────
    if (loHz < LO_FREQ_MIN_HZ || loHz > LO_FREQ_MAX_HZ) {
        Serial.println(F("[ADRF6655] setFrequencyHz out of range (valid: 1050-2300 MHz)"));
        return false;
    }

    // ── N = fLO / fPFD in exact integer arithmetic ────────────────
    // INT is kept 32-bit until it has been range-checked, so a small PFD
    // (e.g. REF_PATH_QUARTER) can no longer wrap a uint8_t into range.
    const uint32_t pfd     = _pfdHz;
    uint32_t       intVal  = (uint32_t)(loHz / pfd);
    const uint32_t remHz   = (uint32_t)(loHz % pfd);

    uint16_t modVal  = _mod;
    uint16_t fracVal;
    if (remHz == 0) {
        fracVal = 0;                         // exact integer N
    } else if (_autoMod) {
        _bestMod(remHz, pfd, modVal, fracVal);
    } else {
        fracVal = _fracFor(remHz, pfd, modVal);
    }

    // Guard: rounding can push FRAC exactly to MOD → carry into INT
    if (fracVal >= modVal) {
        fracVal = 0;
        intVal++;
    }

    // ── Decide mode ───────────────────────────────────────────────
    // FRAC = 0  →  integer mode (DM=1), wider INT range 21–123
    // FRAC ≠ 0  →  fractional mode (DM=0), INT range 24–119
    const bool integerMode = (fracVal == 0);

    // ── Validate INT for the selected mode ────────────────────────
    if (integerMode) {
        if (intVal < INT_INT_MODE_MIN || intVal > INT_INT_MODE_MAX) {
            Serial.println(F("[ADRF6655] INT out of range (integer mode 21-123)"));
            return false;
        }
    } else {
        if (intVal < INT_FRAC_MODE_MIN || intVal > INT_FRAC_MODE_MAX) {
            Serial.println(F("[ADRF6655] INT out of range (fractional mode 24-119)"));
            return false;
        }
    }

    // ── Achieved frequency / error (for getLastFrequencyErrorHz) ──
    {
        uint64_t achieved = (uint64_t)pfd * intVal;
        if (fracVal)
            achieved += ((uint64_t)pfd * fracVal + (modVal >> 1)) / modVal;
        _lastErrHz = (int32_t)((int64_t)achieved - (int64_t)loHz);
    }

    // ── Select LO divider ─────────────────────────────────────────
    const uint8_t loDivider = (loHz >= LO_LDIV_THRESHOLD_HZ)
                              ? LO_DIV_BY_2    // 1530–2300 MHz
                              : LO_DIV_BY_3;   // 1050–1530 MHz

    // ── Deactivate PLL (PLEN=0) ───────────────────────────────────
    deactivate();

    // ── Update LDIV in R5 shadow while PLL is still off ───────────
    // Only the LDIV bit (DB5) is changed; PLEN (DB6) is already 0 from
    // deactivate(). writeRegister() updates _shadow[5] in place.
    {
        uint32_t r5 = _shadow[5];
        if (loDivider == LO_DIV_BY_2) r5 |=  (1UL << 5);   // DB5=1 → ÷2
        else                           r5 &= ~(1UL << 5);   // DB5=0 → ÷3
        writeRegister(5, r5);
    }

    // ── Phase 3: R2 → R1 → wait → PLEN=1 → wait → R0 ────────────
    _phaseThreeSequence(integerMode, (uint8_t)intVal, modVal, fracVal);

    return true;
}

bool ADRF6655::setModulus(uint16_t mod)
{
    if (mod < MOD_MIN || mod > MOD_MAX) return false;
    _mod = mod;
    return true;
}

void ADRF6655::setAutoModulus(bool enable)   { _autoMod = enable; }
int32_t ADRF6655::getLastFrequencyErrorHz() const { return _lastErrHz; }

// ─────────────────────────────────────────────────────────────────
//  setFrequencyManual
// ─────────────────────────────────────────────────────────────────

bool ADRF6655::setFrequencyManual(bool     integerMode,
                                   uint8_t  intVal,
                                   uint16_t modVal,
                                   uint16_t fracVal)
{
    // Validate inputs before touching hardware.  MOD/FRAC are range-checked
    // in both modes because R1/R2 are always written (and cached).
    if (modVal < MOD_MIN || modVal > MOD_MAX) {
        Serial.println(F("[ADRF6655] Manual: MOD out of range (1-2047)"));
        return false;
    }
    if (fracVal > 0x7FF) {
        Serial.println(F("[ADRF6655] Manual: FRAC out of range (0-2047)"));
        return false;
    }
    if (integerMode) {
        if (intVal < INT_INT_MODE_MIN || intVal > INT_INT_MODE_MAX) {
            Serial.println(F("[ADRF6655] Manual: INT out of range (integer 21-123)"));
            return false;
        }
    } else {
        if (intVal < INT_FRAC_MODE_MIN || intVal > INT_FRAC_MODE_MAX) {
            Serial.println(F("[ADRF6655] Manual: INT out of range (fractional 24-119)"));
            return false;
        }
        if (fracVal >= modVal) {
            Serial.println(F("[ADRF6655] Manual: FRAC must be < MOD"));
            return false;
        }
    }

    // Deactivate before changing anything
    deactivate();

    // LDIV is not changed in manual mode; caller must set it via
    // configureR5_LOPathMixer() before calling setFrequencyManual()
    // if the divider needs updating.

    _phaseThreeSequence(integerMode, intVal, modVal, fracVal);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  Getters / setters
// ─────────────────────────────────────────────────────────────────

void ADRF6655::setLockWaitMs(uint16_t ms)       { _lockWaitMs = ms; }
void ADRF6655::setPFDFrequencyMHz(double pfdMHz)
{
    if (pfdMHz > 0.0) _pfdHz = (uint32_t)(pfdMHz * 1e6 + 0.5);
}
double ADRF6655::getPFDFrequencyMHz() const      { return (double)_pfdHz / 1e6; }

uint64_t ADRF6655::getFrequencyHz() const
{
    uint8_t  intVal  = (_shadow[0] >> 3) & 0x7F;
    bool     intMode = (_shadow[0] >> 10) & 0x01;   // DM: MOD/FRAC ignored
    uint16_t modVal  = (_shadow[1] >> 3) & 0x7FF;
    uint16_t fracVal = (_shadow[2] >> 3) & 0x7FF;
    uint64_t hz = (uint64_t)_pfdHz * intVal;
    if (!intMode && modVal != 0 && fracVal != 0)
        hz += ((uint64_t)_pfdHz * fracVal + (modVal >> 1)) / modVal;
    return hz;
}

double ADRF6655::getFrequency() const
{
    return (double)getFrequencyHz() / 1e6;
}

bool ADRF6655::isLocked(uint8_t muxoutPin) const
{
    return (digitalRead(muxoutPin) == HIGH);
}

uint32_t ADRF6655::getShadowReg(uint8_t reg) const
{
    return _shadow[reg & 0x07];
}

// ─────────────────────────────────────────────────────────────────
//  printStatus
// ─────────────────────────────────────────────────────────────────

void ADRF6655::printStatus() const
{
    uint8_t  intVal   = (_shadow[0] >> 3) & 0x7F;
    bool     intMode  = (_shadow[0] >> 10) & 0x01;
    uint16_t modVal   = (_shadow[1] >> 3) & 0x7FF;
    uint16_t fracVal  = (_shadow[2] >> 3) & 0x7FF;
    bool     pll      = (_shadow[5] >> 6) & 0x01;   // PLEN at DB6
    bool     mbe      = (_shadow[5] >> 7) & 0x01;   // MBE  at DB7
    uint8_t  ldiv     = (_shadow[5] >> 5) & 0x01;   // LDIV at DB5
    uint8_t  cdac     = (_shadow[5] >> 8) & 0x0F;   // CDAC at DB11:DB8
    bool     extVCO   = (_shadow[7] >> 22) & 0x01;

    Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
    Serial.println(F("  ADRF6655 Status"));
    Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
    Serial.print(F("  REFIN        : ")); Serial.print((double)_refHz / 1e6, 4); Serial.println(F(" MHz"));
    Serial.print(F("  fPFD         : ")); Serial.print((double)_pfdHz / 1e6, 4); Serial.println(F(" MHz"));
    Serial.print(F("  Mode         : ")); Serial.println(intMode ? F("Integer (DM=1)") : F("Fractional (DM=0)"));
    Serial.print(F("  INT          : ")); Serial.println(intVal);
    Serial.print(F("  MOD          : ")); Serial.println(modVal);
    Serial.print(F("  FRAC         : ")); Serial.println(fracVal);
    Serial.print(F("  LO divider   : ÷")); Serial.println(ldiv ? 2 : 3);
    Serial.print(F("  PLL (PLEN)   : ")); Serial.println(pll ? F("enabled") : F("disabled"));
    Serial.print(F("  Mixer (MBE)  : ")); Serial.println(mbe ? F("on") : F("off"));
    Serial.print(F("  CDAC         : ")); Serial.println(cdac);
    Serial.print(F("  VCO source   : ")); Serial.println(extVCO ? F("external") : F("internal"));
    Serial.print(F("  Computed fLO : ")); Serial.print(getFrequency(), 4); Serial.println(F(" MHz"));
    Serial.print(F("  Last freq err: ")); Serial.print(_lastErrHz); Serial.println(F(" Hz"));
    Serial.print(F("  MOD mode     : ")); Serial.println(_autoMod ? F("auto (best fit)") : F("fixed"));

    // ── R3 / R4 / R6 decode ───────────────────────────────────────
    {
        static const uint8_t ditherMag[4] = {15, 7, 3, 1};
        uint32_t r3 = _shadow[3], r4 = _shadow[4], r6 = _shadow[6];
        Serial.print(F("  R3 dither    : ")); Serial.print((r3 >> 20) & 1 ? F("on") : F("off"));
        Serial.print(F(", mag ")); Serial.print(ditherMag[(r3 >> 21) & 3]);
        Serial.print(F(", restart ")); Serial.println((r3 >> 3) & 0x1FFFFUL);
        Serial.print(F("  R4 MUXOUT    : ")); Serial.print((r4 >> 21) & 7);
        Serial.print(F("  refPath ")); Serial.print((r4 >> 19) & 3);
        Serial.print(F("  CP mult ")); Serial.print(((r4 >> 10) & 3) + 1);
        Serial.print(F("  CPS ")); Serial.print((r4 >> 9) & 1);
        Serial.print(F("  ABD ")); Serial.println((r4 >> 3) & 3);
        Serial.print(F("  R6 enables   : CP=")); Serial.print((r6 >> 20) & 1);
        Serial.print(F(" L3=")); Serial.print((r6 >> 19) & 1);
        Serial.print(F(" LV=")); Serial.print((r6 >> 18) & 1);
        Serial.print(F(" VCO=")); Serial.print((r6 >> 17) & 1);
        Serial.print(F("  amp=")); Serial.print((r6 >> 10) & 0x3F);
        Serial.print(F("  band=")); Serial.print(((r6 >> 9) & 1) ? (int)((r6 >> 3) & 0x3F) : -1);
        Serial.println(F("  (-1 = auto-cal)"));
    }
    Serial.println(F("  ── Shadow registers ──────────────────"));
    for (uint8_t i = 0; i < 8; i++) {
        Serial.print(F("  R")); Serial.print(i); Serial.print(F(" = 0x"));
        uint32_t v = _shadow[i];
        // Always print 6 hex digits (24-bit word)
        char buf[12];
        snprintf(buf, sizeof(buf), "%06lX", (unsigned long)v);
        Serial.println(buf);
    }
    Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
}
