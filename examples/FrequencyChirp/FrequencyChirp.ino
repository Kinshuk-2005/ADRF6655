/*
 * FrequencyChirp.ino
 * ─────────────────────────────────────────────────────────────────
 * ADRF6655 – stepped-frequency chirp / sweep generator.
 *
 * IMPORTANT CAVEAT — read before tuning ramp speed
 * ──────────────────────────────────────────────────
 * Every ADRF6655 frequency change in this driver runs the full
 * deactivate -> reload -> activate sequence:
 *
 *     deactivate()                R5 PLEN=0   (mutes the LO)
 *     R2, R1                      load new divider values
 *     wait > 500 us
 *     activate()                  R5 PLEN=1 + wait > 100 ms
 *     R0                          VCO autocalibration trigger
 *     wait lockWaitMs             PLL settles to the new frequency
 *
 * That 100 ms activation wait plus the lock-settle time happens on
 * EVERY step. This means:
 *
 *   - The LO output is muted (PLEN=0) for part of every step --
 *     this is a STEPPED sweep with gaps, not a continuous-phase
 *     FMCW chirp. There is no fast on-the-fly retune path in this
 *     procedure; the deactivate/reactivate cycle is mandatory on
 *     every change, by design.
 *   - The fastest realistic step rate is roughly
 *         100 ms (activate wait) + lockWaitMs (PLL settle)
 *     i.e. ~105-150 ms per step with default settings.
 *   - A "ramp speed" faster than (frequency span / minimum possible
 *     step time) is NOT achievable. This sketch computes the
 *     achievable rate from your span and step size and tells you
 *     up front, instead of silently running slower than requested.
 *
 * If you need genuinely fast, phase-continuous chirps, this chip's
 * required programming sequence is not the right tool -- consider
 * a dedicated chirp/ramp-capable synthesizer instead.
 *
 * What this sketch does
 * ──────────────────────
 *   1. Initialises the chip.
 *   2. Reduces lockWaitMs to a fast-but-safe value for sweeping
 *      (5 ms; suitable for a wideband/fast loop filter -- raise this
 *      if you are using the datasheet's 1.5 kHz loop filter, which
 *      needs ~50 ms to settle).
 *   3. Computes the number of steps and the achievable ramp rate
 *      from MIN_FREQ_MHZ, MAX_FREQ_MHZ, and FREQ_STEP_MHZ.
 *   4. Sweeps up from MIN to MAX, then back down (triangle wave),
 *      repeating forever. Prints timing stats each pass.
 *
 * Wiring: same as BasicFrequency.ino.
 */

#include <Arduino.h>
#include "ADRF6655.h"

// ── Pin assignments ────────────────────────────────────────────────
static const uint8_t PIN_LE     =  5;
static const uint8_t PIN_MUXOUT =  4;
static const uint8_t PIN_SCK    = 18;
static const uint8_t PIN_MOSI   = 23;

// ── Sweep parameters ──────────────────────────────────────────────
// Valid LO range is 1050-2300 MHz. Keep the span on one side of the
// 1530 MHz LO-divider boundary if you want a perfectly monotonic
// divider setting; setFrequency() switches LDIV automatically at
// that boundary either way, so crossing it is still safe.
static const double MIN_FREQ_MHZ   = 1700.0;
static const double MAX_FREQ_MHZ   = 1800.0;
static const double FREQ_STEP_MHZ  = 2.0;     // resolution per step
static const double DESIRED_RAMP_MHZ_PER_SEC = 500.0;  // what you'd LIKE

static const double REFIN_MHZ = 20.0;

// Use a fast lock wait for sweeping. This assumes a wideband loop
// filter (e.g. 67 kHz per datasheet Fig.68). If you're using the
// datasheet's narrow 1.5 kHz filter, change this to 50.
static const uint16_t SWEEP_LOCK_WAIT_MS = 5;

// Fixed overhead per step that is NOT the lockWait: the 100 ms
// activate() wait plus a small allowance for SPI transfers (three
// 24-bit writes at 10 MHz SPI take only tens of microseconds, so
// this is dominated by the 100 ms wait, not the bus speed).
static const uint32_t ACTIVATE_WAIT_MS = 100;
static const uint32_t SPI_OVERHEAD_MS  = 1;     // rounds up small delays

ADRF6655 synth(PIN_LE, /*spiSpeed=*/10000000);

// ──────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    Serial.println(F("\n=== ADRF6655 Stepped Frequency Chirp ==="));

    synth.begin(REFIN_MHZ, PIN_SCK, PIN_MOSI);
    pinMode(PIN_MUXOUT, INPUT);

    synth.setLockWaitMs(SWEEP_LOCK_WAIT_MS);

    // ── Compute achievable timing ──────────────────────────────────
    double   span     = MAX_FREQ_MHZ - MIN_FREQ_MHZ;
    uint32_t numSteps = (uint32_t)round(span / FREQ_STEP_MHZ) + 1;
    uint32_t stepTimeMs = ACTIVATE_WAIT_MS + SWEEP_LOCK_WAIT_MS + SPI_OVERHEAD_MS;
    uint32_t sweepTimeMs = numSteps * stepTimeMs;
    double   achievableRampRate = span / (sweepTimeMs / 1000.0);  // MHz/s, one direction

    Serial.println(F("── Sweep configuration ──────────────────────"));
    Serial.print(F("  Range          : ")); Serial.print(MIN_FREQ_MHZ);
    Serial.print(F(" - ")); Serial.print(MAX_FREQ_MHZ); Serial.println(F(" MHz"));
    Serial.print(F("  Step size      : ")); Serial.print(FREQ_STEP_MHZ); Serial.println(F(" MHz"));
    Serial.print(F("  Steps per pass : ")); Serial.println(numSteps);
    Serial.print(F("  Time per step  : ~")); Serial.print(stepTimeMs); Serial.println(F(" ms"));
    Serial.print(F("                   (")); Serial.print(ACTIVATE_WAIT_MS);
    Serial.print(F(" ms activate wait + ")); Serial.print(SWEEP_LOCK_WAIT_MS);
    Serial.println(F(" ms lock wait)"));
    Serial.print(F("  One-way sweep  : ~")); Serial.print(sweepTimeMs); Serial.println(F(" ms"));
    Serial.print(F("  Achievable ramp: ~")); Serial.print(achievableRampRate, 1);
    Serial.println(F(" MHz/s"));
    Serial.print(F("  Requested ramp : ")); Serial.print(DESIRED_RAMP_MHZ_PER_SEC); Serial.println(F(" MHz/s"));

    if (achievableRampRate < DESIRED_RAMP_MHZ_PER_SEC) {
        Serial.println(F("  ** WARNING: requested ramp rate is NOT achievable. **"));
        Serial.println(F("  ** Actual rate will be the 'Achievable ramp' value above. **"));
        Serial.println(F("  ** To go faster: increase FREQ_STEP_MHZ (coarser steps), **"));
        Serial.println(F("  ** or reduce SWEEP_LOCK_WAIT_MS if your loop filter allows it. **"));
    } else {
        Serial.println(F("  Requested ramp rate is within the achievable range."));
    }
    Serial.println(F("──────────────────────────────────────────────"));

    delay(1500);  // give time to read the report before sweeping starts
}

// ──────────────────────────────────────────────────────────────────
void loop()
{
    static bool sweepingUp = true;

    uint32_t passStart = millis();
    uint32_t stepCount = 0;

    if (sweepingUp) {
        for (double f = MIN_FREQ_MHZ; f <= MAX_FREQ_MHZ + 0.001; f += FREQ_STEP_MHZ) {
            bool ok = synth.setFrequency(f);
            stepCount++;
            if (!ok) {
                Serial.print(F("Step failed at ")); Serial.print(f); Serial.println(F(" MHz"));
                continue;
            }
            bool locked = synth.isLocked(PIN_MUXOUT);
            Serial.print(F("  -> ")); Serial.print(f, 2); Serial.print(F(" MHz  "));
            Serial.println(locked ? F("[locked]") : F("[NOT LOCKED]"));
        }
    } else {
        for (double f = MAX_FREQ_MHZ; f >= MIN_FREQ_MHZ - 0.001; f -= FREQ_STEP_MHZ) {
            bool ok = synth.setFrequency(f);
            stepCount++;
            if (!ok) {
                Serial.print(F("Step failed at ")); Serial.print(f); Serial.println(F(" MHz"));
                continue;
            }
            bool locked = synth.isLocked(PIN_MUXOUT);
            Serial.print(F("  <- ")); Serial.print(f, 2); Serial.print(F(" MHz  "));
            Serial.println(locked ? F("[locked]") : F("[NOT LOCKED]"));
        }
    }

    uint32_t passTimeMs = millis() - passStart;
    double   actualRate = (MAX_FREQ_MHZ - MIN_FREQ_MHZ) / (passTimeMs / 1000.0);

    Serial.print(F("Pass complete: ")); Serial.print(stepCount); Serial.print(F(" steps in "));
    Serial.print(passTimeMs); Serial.print(F(" ms  ("));
    Serial.print(actualRate, 1); Serial.println(F(" MHz/s actual)"));
    Serial.println();

    sweepingUp = !sweepingUp;   // triangle wave: reverse direction next pass
}
