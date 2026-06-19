#ifndef ADRF6655_H
#define ADRF6655_H

/*
 * ADRF6655.h  –  ESP32 / Arduino SPI driver
 *               Analog Devices ADRF6655 Broadband Up/Downconverting Mixer
 *               with Integrated Fractional-N PLL and VCO.
 *
 * ─────────────────────────────────────────────────────────────────
 *  BIT-POSITION AUTHORITY
 * ─────────────────────────────────────────────────────────────────
 *  All bit positions are reconciled from two sources in this order:
 *    1. Datasheet plain-text register descriptions (pp 26-33), which
 *       are authoritative where they give explicit "DB-n" citations.
 *    2. Figure 69 master overview table, used as ground truth for
 *       registers where the prose and per-register figures disagree.
 *
 *  Key resolved positions (confirmed in prior analysis sessions):
 *    R5  CDAC=DB11:DB8, MBE=DB7, PLEN=DB6, LDIV=DB5, LXL=DB4, LDRV=DB3
 *    R6  reserved=DB23:DB21, CPEN=DB20, L3EN=DB19, LVEN=DB18,
 *        VCOEN=DB17, VCOSW=DB16, VC[5:0]=DB15:DB10,
 *        VBSRC=DB9, VBS[5:0]=DB8:DB3
 *    R1/R2  MOD/FRAC fields span DB13:DB3 (11 bits, <<3 shift)
 *
 * ─────────────────────────────────────────────────────────────────
 *  REGISTER MAP SUMMARY
 * ─────────────────────────────────────────────────────────────────
 *
 *  R0  Integer Divide Control            address bits DB2:DB0 = 000
 *      DB10     DM        0=fractional, 1=integer
 *      DB9:DB3  ID6:ID0   7-bit INT divide ratio
 *                           Fractional mode (DM=0):  INT 24–119
 *                           Integer   mode (DM=1):   INT 21–123
 *
 *  R1  Modulus Divide Control            address bits DB2:DB0 = 001
 *      DB13:DB3  MD10:MD0  11-bit MOD (1–2047)
 *
 *  R2  Fractional Divide Control         address bits DB2:DB0 = 010
 *      DB13:DB3  FD10:FD0  11-bit FRAC (0 to MOD-1)
 *
 *  R3  Σ-Δ Modulator Dither Control      address bits DB2:DB0 = 011
 *      DB22:DB21 DITH1:DITH0  magnitude: 15→00, 7→01, 3→10, 1→11
 *      DB20      DEN           dither enable  (0=off recommended)
 *      DB19:DB3  DV16:DV0     17-bit restart value (use 1)
 *
 *  R4  Charge Pump / PFD / Ref Path      address bits DB2:DB0 = 100
 *      DB23:DB21 RMS2:RMS0   MUXOUT source
 *      DB20:DB19 RS1:RS0     reference path scaling
 *      DB18      CPM         CP ref: 0=internal, 1=RSET pin
 *      DB17      CPBD        phase-offset polarity: 0=neg, 1=pos
 *      DB16:DB12 CPB4:CPB0   phase-offset multiplier (0–31; 4 rec.)
 *      DB11:DB10 CPP1:CPP0   CP current mult 1–4 → 250/500/750/1000µA
 *      DB9       CPS         CP ctrl: 0=PFD driven, 1=force CPC bits
 *      DB8:DB7   CPC1:CPC0   forced CP state (CP_xxx)
 *      DB6       PE1         divider-path edge: 0=falling (rec.)
 *      DB5       PE0         reference-path edge: 0=falling
 *      DB4:DB3   PAB1:PAB0   antibacklash delay (ABD_xxx)
 *
 *  R5  LO Path and Mixer Control         address bits DB2:DB0 = 101
 *      DB11:DB8  CDAC3:CDAC0  distortion compensation 0–15
 *      DB7       MBE          mixer bias enable
 *      DB6       PLEN         PLL enable: 0=off, 1=on
 *      DB5       LDIV         LO divider: 0=÷3, 1=÷2
 *      DB4       LXL          LO path: 0=internal, 1=external LOP/LON
 *      DB3       LDRV         LO output buffer: 0=off (rec.), 1=on
 *
 *  R6  VCO Control and PLL Enables       address bits DB2:DB0 = 110
 *      DB23:DB21 reserved (write 0)
 *      DB20      CPEN     charge pump enable
 *      DB19      L3EN     3.3 V LDO enable
 *      DB18      LVEN     VCO LDO enable
 *      DB17      VCOEN    internal VCO enable
 *      DB16      VCOSW    VCO switch: 0=auto calibration, 1=SPI
 *      DB15:DB10 VC5:VC0  VCO amplitude 0–63 (47 recommended)
 *      DB9       VBSRC    band source: 0=calibration, 1=SPI
 *      DB8:DB3   VBS5:VBS0  VCO band 0–63 (when VBSRC=1)
 *
 *  R7  External VCO Control              address bits DB2:DB0 = 111
 *      DB22  XVCO  0=internal VCO, 1=external VCO at LOP/LON
 *
 * ─────────────────────────────────────────────────────────────────
 *  FREQUENCY FORMULA
 * ─────────────────────────────────────────────────────────────────
 *    fVCO = 2 × fPFD × (INT + FRAC/MOD)
 *    fLO  = fVCO / 2  =  fPFD × (INT + FRAC/MOD)
 *
 *    N = fLO / fPFD  →  INT = floor(N),  FRAC = round(frac(N) × MOD)
 *
 *    MOD is an 11-bit register (max 2047).
 *    With fPFD = 20 MHz and MOD = 1000 → 20 kHz per step.
 *
 * ─────────────────────────────────────────────────────────────────
 *  PROGRAMMING SEQUENCE (every frequency change or power-on)
 * ─────────────────────────────────────────────────────────────────
 *
 *  Phase 2 – Base block init, PLL stays dark:
 *    R7 → R6 → R5(PLEN=0) → R4 → R3
 *
 *  Phase 3 – Frequency load and activation:
 *    R2 → R1 → [wait >500 µs] → R5(PLEN=1) → [wait >100 ms] → R0
 *
 *  The PLEN=1 write sits between R1 and R0 so the charge-pump and
 *  loop-filter supply rails stabilise before the VCO autocalibration
 *  engine fires (triggered by writing R0).
 *
 *  setFrequency() always runs deactivate() first, then Phase 3.
 *  Total per-call overhead ≈ 100 ms + lockWaitMs.
 *
 * ─────────────────────────────────────────────────────────────────
 *  WIRING  (ESP32 VSPI defaults)
 * ─────────────────────────────────────────────────────────────────
 *    ADRF6655  DATA   →  GPIO 23  (MOSI)
 *    ADRF6655  CLK    →  GPIO 18  (SCK)
 *    ADRF6655  LE     →  any GPIO (lePin constructor arg)
 *    ADRF6655  MUXOUT →  any GPIO (optional; lock-detect read-back)
 *    All VCC pins → +5 V, exposed paddle → GND plane (must solder)
 */

#include <Arduino.h>
#include <SPI.h>

// ═══════════════════════════════════════════════════════════════════
//  R4 constants – MUXOUT source  (DB23:DB21)
// ═══════════════════════════════════════════════════════════════════
#define MUXOUT_LOCK_DETECT      0   // HIGH when PLL locked  (default)
#define MUXOUT_VPTAT            1   // Voltage ∝ junction temperature
#define MUXOUT_REFIN_BUFFERED   2   // Buffered REFIN passthrough
#define MUXOUT_REFIN_2X_BUF     3   // 2× buffered REFIN
#define MUXOUT_REFIN_HALF       4   // 0.5× REFIN
// 5, 6 = reserved
#define MUXOUT_TRISTATE         7   // High-impedance output

// ═══════════════════════════════════════════════════════════════════
//  R4 constants – Reference input path scaling  (DB20:DB19)
// ═══════════════════════════════════════════════════════════════════
#define REF_PATH_2X             0   // fPFD = 2 × REFIN
#define REF_PATH_1X             1   // fPFD = REFIN        (default)
#define REF_PATH_HALF           2   // fPFD = 0.5 × REFIN
#define REF_PATH_QUARTER        3   // fPFD = 0.25 × REFIN

// ═══════════════════════════════════════════════════════════════════
//  R4 constants – Forced charge-pump state  (DB8:DB7)
//  Active only when forceChargePumpState = true (CPS bit set).
// ═══════════════════════════════════════════════════════════════════
#define CP_BOTH_ON              0   // Normal: up and down both active
#define CP_PUMP_DOWN            1   // Force pump-down only
#define CP_PUMP_UP              2   // Force pump-up only
#define CP_TRISTATE             3   // Tristate – loop filter holds charge

// ═══════════════════════════════════════════════════════════════════
//  R4 constants – PFD antibacklash delay  (DB4:DB3)
// ═══════════════════════════════════════════════════════════════════
#define ABD_0ns                 0
#define ABD_0p5ns               1
#define ABD_0p75ns              2
#define ABD_0p9ns               3

// ═══════════════════════════════════════════════════════════════════
//  R5 constants – LO divider selection  (DB5)
// ═══════════════════════════════════════════════════════════════════
#define LO_DIV_BY_3             0   // ÷3, use for fLO 1050–1530 MHz
#define LO_DIV_BY_2             1   // ÷2, use for fLO 1530–2300 MHz

// ═══════════════════════════════════════════════════════════════════
//  Frequency limits (datasheet Table 1)
// ═══════════════════════════════════════════════════════════════════
#define LO_FREQ_MIN_MHZ         1050.0
#define LO_FREQ_MAX_MHZ         2300.0
#define LO_LDIV_THRESHOLD_MHZ   1530.0   // ≥ this → ÷2 mode

// ═══════════════════════════════════════════════════════════════════
//  R0 INT field limits
// ═══════════════════════════════════════════════════════════════════
#define INT_FRAC_MODE_MIN        24   // fractional mode (DM=0)
#define INT_FRAC_MODE_MAX       119
#define INT_INT_MODE_MIN         21   // integer mode (DM=1) – wider range
#define INT_INT_MODE_MAX        123

// ═══════════════════════════════════════════════════════════════════
//  R1 MOD field limits and default
// ═══════════════════════════════════════════════════════════════════
#define MOD_MIN                   1
#define MOD_MAX                2047
#define DEFAULT_MOD            1000   // → 20 kHz/step @ 20 MHz PFD


// ═══════════════════════════════════════════════════════════════════
class ADRF6655 {
public:

    // ── Constructor ────────────────────────────────────────────────
    /**
     * @param lePin     GPIO connected to ADRF6655 LE.
     *                  LE idles LOW; pulses HIGH to latch each word.
     * @param spiSpeed  SPI clock in Hz.  Max 20 MHz; default 10 MHz.
     */
    ADRF6655(uint8_t lePin, uint32_t spiSpeed = 10000000);

    // ── begin ──────────────────────────────────────────────────────
    /**
     * @brief Configure GPIO and SPI, then run powerOnSequence().
     *
     * @param refFreqMHz  REFIN oscillator frequency (MHz).  Default 20.
     * @param sckPin      SCK  GPIO; -1 = use board default (18 on ESP32).
     * @param mosiPin     MOSI GPIO; -1 = use board default (23 on ESP32).
     */
    void begin(double refFreqMHz = 20.0,
               int    sckPin     = -1,
               int    mosiPin    = -1);

    // ═══════════════════════════════════════════════════════════════
    //  SINGLE WRITE PRIMITIVE
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief Transmit a 24-bit word to the ADRF6655 and pulse LE.
     *
     * DB2:DB0 of `bitmask` are overwritten with `address` (0–7) before
     * transmission.  Build field data in DB23..DB3 and leave DB2..DB0=0.
     *
     * The transmitted word is cached in _shadow[address] so that
     * deactivate()/activate() can toggle the PLEN bit in isolation
     * without disturbing any other field.
     *
     * @param address  Register 0–7.
     * @param bitmask  24-bit payload.  DB23..DB3 = field values.
     */
    void writeRegister(uint8_t address, uint32_t bitmask);

    // ═══════════════════════════════════════════════════════════════
    //  REGISTER FUNCTIONS  (one per register, R0–R7)
    // ═══════════════════════════════════════════════════════════════

    // ── R0 ─────────────────────────────────────────────────────────
    /**
     * @brief R0 – Integer Divide Control.
     *
     * Writing R0 is the VCO autocalibration hardware trigger.
     * Always write R2 then R1 before calling this.
     *
     * Fields built internally and passed to writeRegister(0, …):
     *   DB10     DM       0=fractional, 1=integer
     *   DB9:DB3  ID6:ID0  7-bit INT
     *
     * Integer mode (DM=1):
     *   INT range 21–123.  R2 and R1 are don't-cares to the divider
     *   hardware, but this driver still writes them for consistency.
     *
     * Fractional mode (DM=0):
     *   INT range 24–119.  R2 and R1 are both actively used.
     *
     * setFrequency() selects integer mode automatically when FRAC = 0.
     *
     * @param integerMode      false=fractional, true=integer.
     * @param integerDivRatio  INT value, clamped to the correct range.
     */
    void configureR0_IntegerDivide(bool    integerMode,
                                    uint8_t integerDivRatio);

    // ── R1 ─────────────────────────────────────────────────────────
    /**
     * @brief R1 – Modulus Divide Control.
     *
     * Fields:  DB13:DB3  MD10:MD0  11-bit MOD (1–2047)
     *
     * Hardware ignores MOD when R0 DM=1 (integer mode); the driver
     * still writes it to keep shadow state and getFrequency() correct.
     *
     * @param modulusValue  MOD value, 1–2047.
     */
    void configureR1_Modulus(uint16_t modulusValue);

    // ── R2 ─────────────────────────────────────────────────────────
    /**
     * @brief R2 – Fractional Divide Control.
     *
     * Fields:  DB13:DB3  FD10:FD0  11-bit FRAC (0 to MOD-1)
     *
     * Write this FIRST in the Phase 3 sequence (before R1).
     * Hardware ignores FRAC when R0 DM=1 (integer mode).
     *
     * @param fractionalValue  FRAC, 0 to (modulusValue-1).
     */
    void configureR2_Fractional(uint16_t fractionalValue);

    // ── R3 ─────────────────────────────────────────────────────────
    /**
     * @brief R3 – Σ-Δ Modulator Dither Control.
     *
     * Fields:
     *   DB22:DB21  DITH1:DITH0  magnitude encoding
     *              15→00, 7→01, 3→10, 1→11  (1 recommended if enabled)
     *   DB20       DEN          dither enable  (0=off is recommended)
     *   DB19:DB3   DV16:DV0    17-bit restart value  (use 1)
     *
     * @param ditherEnable        true to enable SDM dither.
     * @param ditherMagnitude     15, 7, 3, or 1.
     * @param ditherRestartValue  0–131071.  Recommended: 1.
     */
    void configureR3_Dither(bool     ditherEnable,
                             uint8_t  ditherMagnitude,
                             uint32_t ditherRestartValue);

    // ── R4 ─────────────────────────────────────────────────────────
    /**
     * @brief R4 – Charge Pump, PFD, and Reference Path Control.
     *
     * Fields built and passed to writeRegister(4, …):
     *   DB23:DB21  RMS2:RMS0   MUXOUT source          (MUXOUT_xxx)
     *   DB20:DB19  RS1:RS0     reference scaling       (REF_PATH_xxx)
     *   DB18       CPM         CP ref: 0=internal, 1=RSET pin
     *   DB17       CPBD        phase-offset polarity
     *   DB16:DB12  CPB4:CPB0   phase-offset multiplier (0–31; 4 rec.)
     *   DB11:DB10  CPP1:CPP0   CP current mult (1–4)
     *   DB9        CPS         CP ctrl source: 0=PFD, 1=force CPC
     *   DB8:DB7    CPC1:CPC0   forced CP state         (CP_xxx)
     *   DB6        PE1         divider-path edge: 0=falling (rec.)
     *   DB5        PE0         ref-path edge: 0=falling
     *   DB4:DB3    PAB1:PAB0   antibacklash delay      (ABD_xxx)
     *
     * Also updates internal _pfdMHz from refPathSource so that
     * subsequent setFrequency() calls use the correct PFD frequency.
     *
     * @param muxoutSource           MUXOUT_xxx constant
     * @param refPathSource          REF_PATH_xxx constant
     * @param useExternalRset        true = external RSET resistor
     * @param phaseOffsetPositive    true = positive polarity (recommended)
     * @param phaseOffsetMultiplier  0–31; datasheet recommends 4
     * @param cpCurrentMultiplier    1, 2, 3, or 4  (→ 250/500/750/1000 µA)
     * @param forceChargePumpState   false = PFD controls CP (normal use)
     * @param cpForceState           CP_xxx (used only when forceChargePumpState=true)
     * @param dividerEdgeFalling     true = falling edge (recommended)
     * @param refEdgeFalling         true = falling edge
     * @param antibacklashDelay      ABD_xxx constant
     */
    void configureR4_ChargePumpPFD(uint8_t muxoutSource,
                                    uint8_t refPathSource,
                                    bool    useExternalRset,
                                    bool    phaseOffsetPositive,
                                    uint8_t phaseOffsetMultiplier,
                                    uint8_t cpCurrentMultiplier,
                                    bool    forceChargePumpState,
                                    uint8_t cpForceState,
                                    bool    dividerEdgeFalling,
                                    bool    refEdgeFalling,
                                    uint8_t antibacklashDelay);

    // ── R5 ─────────────────────────────────────────────────────────
    /**
     * @brief R5 – LO Path and Mixer Control.
     *
     * Fields (all verified against datasheet prose, p.31):
     *   DB11:DB8  CDAC3:CDAC0  distortion compensation 0–15
     *                           Use 15 for RF < 1840 MHz (safe default)
     *                           See Fig.64 for RF > 1840 MHz
     *   DB7       MBE          mixer bias enable  ("address (DB7)" prose)
     *   DB6       PLEN         PLL enable         ("address (DB6)" prose)
     *   DB5       LDIV         LO divider: 0=÷3, 1=÷2
     *   DB4       LXL          LO path: 0=internal output, 1=external input
     *   DB3       LDRV         LO output buffer: 0=off (recommended)
     *
     * deactivate()/activate() toggle ONLY the PLEN bit (DB6) while
     * preserving every other field already in the shadow register.
     *
     * @param mixerBiasEnable    true = mixer on (default)
     * @param cdacValue          0–15
     * @param pllEnable          true = PLL on
     * @param loDivider          LO_DIV_BY_2 or LO_DIV_BY_3
     * @param loExternal         false = internal VCO, true = ext. LOP/LON
     * @param loOutputDriverOn   true = LO appears at LOP/LON for probing;
     *                           keep false for best RF performance & power
     */
    void configureR5_LOPathMixer(bool    mixerBiasEnable,
                                  uint8_t cdacValue,
                                  bool    pllEnable,
                                  uint8_t loDivider,
                                  bool    loExternal,
                                  bool    loOutputDriverOn);

    // ── R6 ─────────────────────────────────────────────────────────
    /**
     * @brief R6 – VCO Control and PLL Enables.
     *
     * Fields (all verified against Fig.69 master table):
     *   DB23:DB21  reserved (write 0)
     *   DB20       CPEN     charge pump enable
     *   DB19       L3EN     3.3 V LDO enable
     *   DB18       LVEN     VCO LDO enable
     *   DB17       VCOEN    internal VCO enable
     *   DB16       VCOSW    VCO switch: 0=auto calibration, 1=SPI manual
     *   DB15:DB10  VC5:VC0  VCO amplitude 0–63 (47 recommended)
     *   DB9        VBSRC    band source: 0=calibration result, 1=SPI
     *   DB8:DB3    VBS5:VBS0  VCO band 0–63 (used when VBSRC=1)
     *
     * @param chargePumpEnable  true = CP on
     * @param ldo3v3Enable      true = 3.3 V LDO on
     * @param vcoLdoEnable      true = VCO LDO on
     * @param vcoEnable         true = internal VCO on
     * @param vcoSwitchManual   false = auto band calibration (default)
     * @param vcoAmplitude      0–63; 47 recommended
     * @param vcoBandFromSPI    false = calibration selects band
     * @param vcoBandSelect     0–63; only used when vcoBandFromSPI=true
     */
    void configureR6_VCOControl(bool    chargePumpEnable,
                                 bool    ldo3v3Enable,
                                 bool    vcoLdoEnable,
                                 bool    vcoEnable,
                                 bool    vcoSwitchManual,
                                 uint8_t vcoAmplitude,
                                 bool    vcoBandFromSPI,
                                 uint8_t vcoBandSelect);

    // ── R7 ─────────────────────────────────────────────────────────
    /**
     * @brief R7 – External VCO Control.
     *
     * Fields:
     *   DB22  XVCO  0=internal VCO (default), 1=external VCO at LOP/LON
     *   All other bits reserved; written as 0.
     *
     * When switching to external VCO:
     *   1. configureR7_ExternalVCO(true)
     *   2. configureR6_VCOControl(…, vcoEnable=false, …) – powers down
     *      the internal VCO core and saves ~20 mA.
     *   External signal: >-10 dBm, ac-coupled, differential,
     *   frequency = 2× or 3× desired LO (matches LDIV setting).
     *
     * @param useExternalVCO  false=internal (default), true=external
     */
    void configureR7_ExternalVCO(bool useExternalVCO);

    // ═══════════════════════════════════════════════════════════════
    //  PLL ACTIVATION / DEACTIVATION
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief Deactivate the PLL: sets PLEN=0 in R5 (DB6).
     *
     * Immediately breaks the feedback loop, shuts down the charge
     * pump, and mutes the LO.  All other R5 fields (CDAC, MBE, LDIV,
     * LXL, LDRV) are read from the shadow register and preserved.
     *
     * Called automatically at the start of every setFrequency().
     */
    void deactivate();

    /**
     * @brief Activate the PLL: sets PLEN=1 in R5 (DB6), then waits
     *        100 ms for supply lines to stabilise.
     *
     * Called automatically inside the Phase 3 sequence, between
     * writing R1 and writing R0.  Call manually only if you deactivated
     * the chip for standby and need to wake it without a setFrequency().
     */
    void activate();

    /**
     * @brief Deep standby: PLL off and all VCO/LDO/CP blocks off.
     *
     * Sequence: deactivate() → R6 with every enable=false.
     * To recover: wakeFromStandby() then setFrequency().
     */
    void enterDeepStandby();

    /**
     * @brief Restore R6 to its normal operating state after deep standby.
     *
     * Re-enables CP, both LDOs, and the internal VCO (amplitude 47,
     * auto band calibration).  Does NOT re-enable the PLL – call
     * setFrequency() after this to retune and lock.
     */
    void wakeFromStandby();

    // ═══════════════════════════════════════════════════════════════
    //  FREQUENCY CONTROL
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief Set the LO output frequency automatically.
     *
     * Runs on every call:
     *   1. deactivate()                           R5 PLEN=0
     *   2. Update LDIV in R5 shadow if needed     R5 (PLEN still 0)
     *   3. configureR2_Fractional(FRAC)           R2
     *   4. configureR1_Modulus(MOD)               R1
     *   5. delay 600 µs
     *   6. activate()                             R5 PLEN=1 + 100 ms
     *   7. configureR0_IntegerDivide(…)           R0  ← cal trigger
     *   8. delay lockWaitMs
     *
     * Mode/divider selection (automatic):
     *   FRAC = 0  →  integer mode (DM=1), INT 21–123
     *   FRAC ≠ 0  →  fractional mode (DM=0), INT 24–119
     *   fLO ≥ 1530 MHz  →  LO_DIV_BY_2
     *   fLO <  1530 MHz →  LO_DIV_BY_3
     *
     * @param loFreqMHz  Target LO frequency in MHz (1050–2300).
     * @return true on success, false if out of range.
     */
    bool setFrequency(double loFreqMHz);

    /**
     * @brief Set frequency using explicit INT / MOD / FRAC values.
     *
     * Runs the same Phase 3 sequence as setFrequency() but with values
     * you supply directly – no internal calculation.  Useful for
     * scripted sweeps, register-dump replay, or testing specific codes.
     *
     * @param integerMode  false=fractional (DM=0), true=integer (DM=1)
     * @param intVal       INT.  21–123 (integer) or 24–119 (fractional)
     * @param modVal       MOD.  1–2047.  Hardware ignores when DM=1
     * @param fracVal      FRAC. 0 to modVal-1.  Hardware ignores when DM=1
     * @return true if all values pass range validation; false otherwise
     *         (nothing is written if validation fails)
     */
    bool setFrequencyManual(bool     integerMode,
                             uint8_t  intVal,
                             uint16_t modVal,
                             uint16_t fracVal);

    /**
     * @brief Set lock wait time used after writing R0 (calibration trigger).
     *
     * 1.5 kHz loop filter → 50 ms
     * 67  kHz loop filter →  5 ms
     * Default             → 20 ms
     *
     * @param ms  Milliseconds to wait.
     */
    void setLockWaitMs(uint16_t ms);

    /**
     * @brief Manually override the PFD frequency for setFrequency().
     *
     * configureR4_ChargePumpPFD() sets this automatically.
     * @param pfdMHz  PFD frequency in MHz.
     */
    void setPFDFrequencyMHz(double pfdMHz);

    /** @return Effective PFD frequency in MHz. */
    double getPFDFrequencyMHz() const;

    /** @return LO frequency computed from shadow registers (MHz). */
    double getFrequency() const;

    /**
     * @brief Read lock-detect status from the MUXOUT pin.
     *
     * Requires R4 MUXOUT source = MUXOUT_LOCK_DETECT (default).
     * Caller must configure the pin as INPUT before calling.
     *
     * @param muxoutPin  GPIO wired to ADRF6655 MUXOUT.
     * @return true if locked (pin reads HIGH).
     */
    bool isLocked(uint8_t muxoutPin) const;

    // ═══════════════════════════════════════════════════════════════
    //  INITIALISATION
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief Run the full two-phase initialisation sequence.
     *
     * Called automatically by begin().  Call again after a power
     * glitch or if the PLL needs a hard reset.
     *
     * Phase 2: R7 → R6 → R5(PLEN=0) → R4 → R3
     * Phase 3: R2 → R1 → [>500µs] → R5(PLEN=1) → [>100ms] → R0
     *
     * A placeholder frequency of INT=87 (1740 MHz @ 20 MHz PFD)
     * is programmed.  Call setFrequency() afterward.
     */
    void powerOnSequence();

    // ═══════════════════════════════════════════════════════════════
    //  UTILITIES
    // ═══════════════════════════════════════════════════════════════

    /** @brief Dump decoded register state and computed fLO to Serial. */
    void printStatus() const;

    /**
     * @brief Return the shadow-cached value of any register.
     * @param reg  0–7.
     */
    uint32_t getShadowReg(uint8_t reg) const;


private:
    uint8_t   _lePin;
    uint32_t  _spiSpeed;
    double    _refMHz;
    double    _pfdMHz;
    uint16_t  _lockWaitMs;
    uint32_t  _shadow[8];

    void _spiSend(uint32_t word24);

    // Shared Phase-3 executor called by setFrequency() and setFrequencyManual().
    // Caller must already have: (a) called deactivate(), (b) updated LDIV in
    // the R5 shadow if the divider mode changed.
    void _phaseThreeSequence(bool     integerMode,
                              uint8_t  intVal,
                              uint16_t modVal,
                              uint16_t fracVal);
};

#endif // ADRF6655_H
