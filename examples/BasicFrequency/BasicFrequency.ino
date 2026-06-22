/*
 * BasicFrequency.ino
 * ─────────────────────────────────────────────────────────────────
 * ADRF6655 – simplest possible usage example.
 *
 * What this sketch does
 * ──────────────────────
 *   1. Runs the full power-on sequence (Phase 2 + Phase 3) via begin().
 *   2. Sets one fixed LO frequency using setFrequency(), which
 *      internally performs: deactivate -> R2 -> R1 -> wait>500us ->
 *      activate(PLEN=1) -> wait>100ms -> R0 -> wait lockWaitMs.
 *   3. Checks PLL lock via the MUXOUT pin.
 *   4. Prints the decoded register state to the Serial Monitor.
 *   5. Loops, periodically reporting lock status.
 *
 * Wiring (ESP32 DevKit, VSPI)
 * ──────────────────────────
 *   ADRF6655  DATA   ->  GPIO 23  (MOSI)
 *   ADRF6655  CLK    ->  GPIO 18  (SCK)
 *   ADRF6655  LE     ->  GPIO  5
 *   ADRF6655  MUXOUT ->  GPIO  4  (lock-detect read-back)
 *   ADRF6655  REFIN  <- 20 MHz oscillator
 *   ADRF6655  VCC1/VCC2/VCCLO/VCCMIX/VCCV2I -> +5 V
 *   Exposed paddle -> GND plane (must be soldered)
 *
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include "ADRF6655.h"

// ── Pin assignments ────────────────────────────────────────────────
static const uint8_t PIN_LE     =  5;
static const uint8_t PIN_MUXOUT =  4;
static const uint8_t PIN_SCK    = 18;
static const uint8_t PIN_MOSI   = 23;

// ── Target LO frequency ───────────────────────────────────────────
// Valid range: 1050-2300 MHz.
//   Downconversion: LO = RF +/- IF
//   Upconversion:   LO = IF_out +/- IF_in
static const double TARGET_LO_MHZ = 1740.0;  // -> RF=1880 MHz, IF=140 MHz example

static const double REFIN_MHZ = 20.0;

ADRF6655 synth(PIN_LE, /*spiSpeed=*/10000000);

// ──────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    Serial.println(F("\n=== ADRF6655 Basic Frequency Example ==="));

    // begin() configures GPIO/SPI then runs powerOnSequence():
    //   Phase 2: R7 -> R6 -> R5(PLEN=0) -> R4 -> R3
    //   Phase 3: R2 -> R1 -> wait -> R5(PLEN=1) -> wait -> R0
    synth.begin(REFIN_MHZ, PIN_SCK, PIN_MOSI);

    pinMode(PIN_MUXOUT, INPUT);   // MUXOUT defaults to lock-detect

    // Lock-wait after the R0 calibration trigger. Match to your loop
    // filter: 1.5 kHz filter -> 50 ms; 67 kHz filter -> 5 ms.
    synth.setLockWaitMs(20);

    // ── Optional: tune CDAC for your actual RF frequency ──────────
    // 15 is the safe default for RF < 1840 MHz. For higher RF, see
    // datasheet Figure 64 for the recommended CDAC vs frequency curve.
    // Calling this rewrites R5 in full, so every field is supplied.
    // Note: this does NOT touch PLEN -- deactivate()/activate() are
    // the only functions that toggle PLEN in isolation, so calling
    // configureR5 directly while the PLL is locked will also change
    // PLEN unless you pass back its current state. Read it from the
    // shadow register first if you want to preserve lock:
    {
        uint32_t r5 = synth.getShadowReg(5);
        bool pllCurrentlyOn = (r5 >> 6) & 0x01;
        synth.configureR5_LOPathMixer(
            /*mixerBias=*/  true,
            /*cdac=*/       15,
            /*pllEnable=*/  pllCurrentlyOn,  // preserve current PLL state
            /*loDivider=*/  LO_DIV_BY_2,     // setFrequency() will override this anyway
            /*loExternal=*/ false,
            /*loDriver=*/   false
        );
    }

    // ── Set the target frequency ───────────────────────────────────
    // setFrequency() runs the full deactivate -> reload -> activate
    // sequence and picks integer vs fractional mode automatically.
    Serial.print(F("Setting LO to "));
    Serial.print(TARGET_LO_MHZ);
    Serial.println(F(" MHz ..."));

    bool ok = synth.setFrequency(TARGET_LO_MHZ);

    if (!ok) {
        Serial.println(F("ERROR: setFrequency failed (out of range?)"));
        while (true) { delay(1000); }
    }

    bool locked = synth.isLocked(PIN_MUXOUT);
    Serial.print(F("PLL lock detect (MUXOUT): "));
    Serial.println(locked ? F("LOCKED") : F("NOT LOCKED - check loop filter / REFIN"));

    synth.printStatus();

    Serial.println(F("Setup complete. LO is running."));
}

// ──────────────────────────────────────────────────────────────────
void loop()
{
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 5000) {
        lastPrint = millis();
        bool locked = synth.isLocked(PIN_MUXOUT);
        Serial.print(F("fLO = "));
        Serial.print(synth.getFrequency(), 4);
        Serial.print(F(" MHz  |  PLL: "));
        Serial.println(locked ? F("LOCKED") : F("UNLOCKED"));
    }
}
