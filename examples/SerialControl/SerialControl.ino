/*
 * SerialControl.ino
 * ─────────────────────────────────────────────────────────────────
 * ADRF6655 – interactive Serial Monitor control.
 *
 * Type commands into the Serial Monitor (115200 baud, newline ending)
 * to control the chip live. Useful for bench testing, exploring valid
 * frequency ranges, or driving the chip from a PC script over USB.
 *
 * Commands
 * ────────
 *   f <MHz>                  Set LO frequency automatically.
 *                             e.g.  f 1740.5
 *
 *   m <int|frac> <INT> <MOD> <FRAC>
 *                             Set frequency manually with explicit
 *                             register values. MOD/FRAC are ignored
 *                             by the hardware in integer mode but are
 *                             still required as arguments (use any
 *                             valid placeholder, e.g. 1000 0).
 *                             e.g.  m frac 87 1000 500
 *                                   m int  90 1000 0
 *
 *   mod <1-2047>             Fixed MOD used by 'f' (default 1000 = 20 kHz
 *                             steps at a 20 MHz PFD).  e.g.  mod 2000
 *
 *   automod <on|off>         Search MOD 1..2047 for the smallest frequency
 *                             error on every 'f'.  Default off.
 *
 *   status                   Print full decoded register status.
 *
 *   lock                     Print current PLL lock state.
 *
 *   lockwait <ms>             Set the post-R0 lock wait time.
 *                             e.g.  lockwait 50
 *
 *   mux <source>              Set MUXOUT signal source. <source> is
 *                             one of: lock, vptat, refin, refin2x,
 *                             refinhalf, tristate
 *                             e.g.  mux vptat
 *
 *   standby                  Enter deep standby (PLL off, VCO/LDO/CP off).
 *
 *   wake                     Restore R6 to normal operation after
 *                             standby. Follow with an 'f' command to
 *                             retune and relock.
 *
 *   help                     Print this command list.
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

static const double REFIN_MHZ = 20.0;

ADRF6655 synth(PIN_LE, /*spiSpeed=*/10000000);

// ── Small line-buffered serial reader ───────────────────────────────
String inputLine;

void printHelp()
{
    Serial.println(F("──────────────────────────────────────────────"));
    Serial.println(F("  ADRF6655 Serial Control - commands"));
    Serial.println(F("──────────────────────────────────────────────"));
    Serial.println(F("  f <MHz>                          set LO frequency"));
    Serial.println(F("  m <int|frac> <INT> <MOD> <FRAC>   manual register set"));
    Serial.println(F("  mod <1-2047>                      fixed MOD for 'f'"));
    Serial.println(F("  automod <on|off>                  best-fit MOD search"));
    Serial.println(F("  status                            full register dump"));
    Serial.println(F("  lock                              show PLL lock state"));
    Serial.println(F("  lockwait <ms>                     set post-R0 wait"));
    Serial.println(F("  mux <lock|vptat|refin|refin2x|refinhalf|tristate>"));
    Serial.println(F("  standby                           deep standby (PLL+VCO off)"));
    Serial.println(F("  wake                              restore from standby"));
    Serial.println(F("  help                              show this list"));
    Serial.println(F("──────────────────────────────────────────────"));
}

// Parse a decimal MHz string ("1740", "1740.5", "1740.012345") into exact Hz
// using integer arithmetic only (String::toFloat() is a 32-bit float and
// would lose ~100 Hz at 1.7 GHz).  Digits past the 6th decimal are dropped.
static bool parseMHzToHz(const String &s, uint64_t &hz)
{
    uint64_t whole = 0, frac = 0;
    uint32_t scale = 1000000UL;
    int i = 0, n = s.length();
    bool any = false;
    for (; i < n && isDigit(s[i]); i++) {
        whole = whole * 10 + (uint64_t)(s[i] - '0');
        any = true;
        if (whole > 100000ULL) return false;
    }
    if (i < n && s[i] == '.') {
        i++;
        for (; i < n && isDigit(s[i]); i++) {
            any = true;
            if (scale > 1) { scale /= 10; frac += (uint64_t)(s[i] - '0') * scale; }
        }
    }
    if (i < n || !any) return false;
    hz = whole * 1000000ULL + frac;
    return true;
}

void handleCommand(String line)
{
    line.trim();
    if (line.length() == 0) return;

    // Tokenize on spaces (simple split, no quoting needed for this command set)
    const int MAX_TOKENS = 5;
    String tokens[MAX_TOKENS];
    int tokenCount = 0;
    int start = 0;
    while (tokenCount < MAX_TOKENS) {
        int spaceIdx = line.indexOf(' ', start);
        if (spaceIdx == -1) {
            tokens[tokenCount++] = line.substring(start);
            break;
        }
        tokens[tokenCount++] = line.substring(start, spaceIdx);
        start = spaceIdx + 1;
        while (start < (int)line.length() && line[start] == ' ') start++;
    }

    String cmd = tokens[0];
    cmd.toLowerCase();

    // ── f <MHz> ──────────────────────────────────────────────────
    if (cmd == "f") {
        if (tokenCount < 2) { Serial.println(F("Usage: f <MHz>")); return; }
        uint64_t hz;
        if (!parseMHzToHz(tokens[1], hz)) { Serial.println(F("Could not parse frequency.")); return; }
        Serial.print(F("Setting LO to ")); Serial.print(tokens[1]); Serial.println(F(" MHz ..."));
        uint32_t t0 = millis();
        bool ok = synth.setFrequencyHz(hz);
        uint32_t elapsed = millis() - t0;
        if (ok) {
            bool locked = synth.isLocked(PIN_MUXOUT);
            Serial.print(F("Done in ")); Serial.print(elapsed); Serial.print(F(" ms. PLL: "));
            Serial.print(locked ? F("LOCKED") : F("NOT LOCKED"));
            Serial.print(F("  (achieved ")); Serial.print(synth.getFrequency(), 6);
            Serial.print(F(" MHz, error ")); Serial.print(synth.getLastFrequencyErrorHz());
            Serial.println(F(" Hz)"));
        } else {
            Serial.println(F("Failed: frequency out of range (1050-2300 MHz)."));
        }
    }

    // ── m <int|frac> <INT> <MOD> <FRAC> ─────────────────────────
    else if (cmd == "m") {
        if (tokenCount < 5) {
            Serial.println(F("Usage: m <int|frac> <INT> <MOD> <FRAC>"));
            return;
        }
        String modeStr = tokens[1];
        modeStr.toLowerCase();
        bool integerMode;
        if (modeStr == "int")       integerMode = true;
        else if (modeStr == "frac") integerMode = false;
        else { Serial.println(F("Mode must be 'int' or 'frac'")); return; }

        uint8_t  intVal  = (uint8_t)tokens[2].toInt();
        uint16_t modVal  = (uint16_t)tokens[3].toInt();
        uint16_t fracVal = (uint16_t)tokens[4].toInt();

        Serial.print(F("Manual set: mode="));
        Serial.print(integerMode ? F("integer") : F("fractional"));
        Serial.print(F(" INT=")); Serial.print(intVal);
        Serial.print(F(" MOD=")); Serial.print(modVal);
        Serial.print(F(" FRAC=")); Serial.println(fracVal);

        bool ok = synth.setFrequencyManual(integerMode, intVal, modVal, fracVal);
        if (ok) {
            bool locked = synth.isLocked(PIN_MUXOUT);
            Serial.print(F("Applied. Computed fLO=")); Serial.print(synth.getFrequency(), 4);
            Serial.print(F(" MHz. PLL: ")); Serial.println(locked ? F("LOCKED") : F("NOT LOCKED"));
        } else {
            Serial.println(F("Failed: values out of range for selected mode."));
        }
    }

    // ── status ───────────────────────────────────────────────────
    else if (cmd == "status") {
        synth.printStatus();
    }

    // ── lock ─────────────────────────────────────────────────────
    else if (cmd == "lock") {
        bool locked = synth.isLocked(PIN_MUXOUT);
        Serial.print(F("PLL lock: ")); Serial.println(locked ? F("LOCKED") : F("NOT LOCKED"));
    }

    // ── mod <n> ──────────────────────────────────────────────────
    else if (cmd == "mod") {
        if (tokenCount < 2) { Serial.println(F("Usage: mod <1-2047>")); return; }
        long m = tokens[1].toInt();
        if (m >= 1 && m <= 2047 && synth.setModulus((uint16_t)m)) {
            Serial.print(F("Fixed MOD set to ")); Serial.println(m);
        } else {
            Serial.println(F("MOD must be 1-2047."));
        }
    }

    // ── automod <on|off> ─────────────────────────────────────────
    else if (cmd == "automod") {
        if (tokenCount < 2) { Serial.println(F("Usage: automod <on|off>")); return; }
        String v = tokens[1]; v.toLowerCase();
        if      (v == "on")  { synth.setAutoModulus(true);  Serial.println(F("Auto-MOD on.")); }
        else if (v == "off") { synth.setAutoModulus(false); Serial.println(F("Auto-MOD off (fixed MOD).")); }
        else Serial.println(F("Usage: automod <on|off>"));
    }

    // ── lockwait <ms> ────────────────────────────────────────────
    else if (cmd == "lockwait") {
        if (tokenCount < 2) { Serial.println(F("Usage: lockwait <ms>")); return; }
        uint16_t ms = (uint16_t)tokens[1].toInt();
        synth.setLockWaitMs(ms);
        Serial.print(F("Lock wait set to ")); Serial.print(ms); Serial.println(F(" ms"));
    }

    // ── mux <source> ─────────────────────────────────────────────
    else if (cmd == "mux") {
        if (tokenCount < 2) { Serial.println(F("Usage: mux <lock|vptat|refin|refin2x|refinhalf|tristate>")); return; }
        String src = tokens[1]; src.toLowerCase();
        uint8_t muxVal;
        if      (src == "lock")       muxVal = MUXOUT_LOCK_DETECT;
        else if (src == "vptat")      muxVal = MUXOUT_VPTAT;
        else if (src == "refin")      muxVal = MUXOUT_REFIN_BUFFERED;
        else if (src == "refin2x")    muxVal = MUXOUT_REFIN_2X_BUF;
        else if (src == "refinhalf")  muxVal = MUXOUT_REFIN_HALF;
        else if (src == "tristate")   muxVal = MUXOUT_TRISTATE;
        else { Serial.println(F("Unknown MUXOUT source")); return; }

        // configureR4 rewrites the whole register; reuse current
        // reference-path / charge-pump settings from the shadow value
        // so we only change the MUXOUT source field.
        uint32_t r4 = synth.getShadowReg(4);
        uint8_t  refPath        = (r4 >> 19) & 0x03;
        bool     extRset        = (r4 >> 18) & 0x01;
        bool     posOffset      = (r4 >> 17) & 0x01;
        uint8_t  offMult        = (r4 >> 12) & 0x1F;
        uint8_t  cppBits        = (r4 >> 10) & 0x03;
        bool     forceCP        = (r4 >> 9)  & 0x01;
        uint8_t  cpForceState   = (r4 >> 7)  & 0x03;
        bool     divFalling     = !((r4 >> 6) & 0x01);
        bool     refFalling     = !((r4 >> 5) & 0x01);
        uint8_t  abd            = (r4 >> 3)  & 0x03;

        synth.configureR4_ChargePumpPFD(
            muxVal, refPath, extRset, posOffset, offMult,
            (cppBits + 1), forceCP, cpForceState,
            divFalling, refFalling, abd
        );
        Serial.print(F("MUXOUT source set to: ")); Serial.println(src);
    }

    // ── standby ──────────────────────────────────────────────────
    else if (cmd == "standby") {
        synth.enterDeepStandby();
        Serial.println(F("Entered deep standby (PLL + VCO + LDOs off)."));
    }

    // ── wake ─────────────────────────────────────────────────────
    else if (cmd == "wake") {
        synth.wakeFromStandby();
        Serial.println(F("Woke from standby. Send 'f <MHz>' to retune and relock."));
    }

    // ── help ─────────────────────────────────────────────────────
    else if (cmd == "help") {
        printHelp();
    }

    else {
        Serial.print(F("Unknown command: ")); Serial.println(cmd);
        Serial.println(F("Type 'help' for the command list."));
    }
}

// ──────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    Serial.println(F("\n=== ADRF6655 Serial Control ==="));

    synth.begin(REFIN_MHZ, PIN_SCK, PIN_MOSI);
    pinMode(PIN_MUXOUT, INPUT);
    synth.setLockWaitMs(20);

    Serial.println(F("Chip initialised."));
    printHelp();
    Serial.print(F("> "));
}

// ──────────────────────────────────────────────────────────────────
void loop()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (inputLine.length() > 0) {
                Serial.println(inputLine);   // echo
                handleCommand(inputLine);
                inputLine = "";
                Serial.print(F("> "));
            }
        } else {
            inputLine += c;
        }
    }
}
