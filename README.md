# ADRF6655 Arduino Library

ESP32 / Arduino SPI driver for the Analog Devices **ADRF6655** broadband
up/downconverting mixer with integrated fractional-N PLL and VCO.

## Installation

**Arduino IDE:** *Sketch → Include Library → Manage Libraries…*, search for
`ADRF6655`. Or download a release zip and use *Add .ZIP Library…*.

**PlatformIO:** `lib_deps = https://github.com/Kinshuk-2005/ADRF6655`

Supported: ESP32 (primary, hardware-verified). AVR is declared in
`library.properties` but has not been hardware-tested; see [AVR notes](#avr-notes).

See [CHANGELOG.md](CHANGELOG.md) for what changed between versions.

## Architecture

- **`writeRegister(address, bitmask)`** — the single low-level function
  that sends a 24-bit word over SPI and latches it with LE. Every other
  function funnels through this one.
- **`configureR0_IntegerDivide()` … `configureR7_ExternalVCO()`** — one
  function per register. Each takes every field of that register as a
  named parameter, builds the bitmask, and calls `writeRegister()`.
  Each returns `true` if the word was written, or `false` if an argument
  was out of range (nothing is sent in that case).
- **`setFrequency(loFreqMHz)`** — computes `INT`, `MOD`, `FRAC` from a
  target frequency, decides integer vs. fractional mode automatically,
  and runs the full deactivate → reload → activate sequence.
- **`setFrequencyHz(hz)`** — same as `setFrequency()` but takes an exact
  `uint64_t` frequency in Hz. All divider maths is 64-bit integer, so the
  result is exact on AVR as well.
- **`setFrequencyManual(integerMode, INT, MOD, FRAC)`** — same sequence,
  but with values you supply directly instead of calculating them.

## Required register sequence

Every frequency change — including the very first one at power-up —
follows the same pattern. The chip is fully deactivated, the new
divider values are loaded, and the PLL is reactivated before the
calibration trigger fires:

```
deactivate()                      R5  PLEN = 0
configureR2_Fractional(FRAC)      R2
configureR1_Modulus(MOD)          R1
wait > 500 µs
activate()                        R5  PLEN = 1
wait > 100 ms
configureR0_IntegerDivide(...)    R0   ← VCO autocalibration trigger
wait lockWaitMs
```

`setFrequency()`, `setFrequencyHz()` and `setFrequencyManual()` all run this
sequence internally — you don't need to call the steps yourself. Plan for
roughly **100 ms + lockWaitMs** of latency on every retune; the deep
deactivate/reactivate cycle is part of the procedure, not an
optional safety margin.

Power-up (`begin()` → `powerOnSequence()`) additionally writes the
static blocks before the frequency load:

```
R7 → R6 → R5(PLEN=0) → R4 → R3   (base block init, PLL dark)
R2 → R1 → wait → R5(PLEN=1) → wait → R0   (frequency load + activation)
```

The second line parks the PLL at a 1740 MHz placeholder. Pass
`autoStart = false` to `begin()` to skip it; see
[Starting without the placeholder lock](#starting-without-the-placeholder-lock).

## Integer vs. fractional mode

Register 0's `DM` bit changes which `INT` range is valid, and makes
`MOD`/`FRAC` don't-cares to the divider hardware:

| Mode | DM | INT range | MOD / FRAC |
|---|---|---|---|
| Fractional | 0 | 24–119 | both used |
| Integer | 1 | **21–123** (wider) | ignored by hardware |

`setFrequency()` picks integer mode automatically whenever the target
frequency divides the PFD frequency with zero remainder, or rounds to it
on the MOD grid (`FRAC == 0`),
and validates `INT` against the correct range for whichever mode gets
selected. `setFrequencyManual()` validates the same way for whichever
mode you pass in explicitly.

## Frequency resolution and accuracy

With the default fixed `MOD = 1000` and a 20 MHz PFD the frequency grid is
20 kHz. A request that is not on the grid is rounded to the nearest step;
the driver tells you by how much:

```cpp
synth.setFrequencyHz(1740012345ULL);
int32_t err = synth.getLastFrequencyErrorHz();   // achieved − requested, Hz
double  f   = synth.getFrequency();              // achieved, MHz (from shadow regs)
```

Two ways to do better:

```cpp
synth.setModulus(2000);        // different fixed MOD (1–2047)
synth.setAutoModulus(true);    // search MOD 1..2047 for the smallest error
```

Auto-MOD is **off by default** so behaviour is identical to v1.0.x. The
search is a few hundred ms of CPU on AVR (64-bit division) and about a
millisecond on ESP32. A different MOD changes the fractional-spur
pattern, so check your spectrum if that matters for your application.

## Starting without the placeholder lock

`begin()` normally parks the PLL at a 1740 MHz placeholder (≈120 ms) before
your first `setFrequency()`. To skip that:

```cpp
synth.begin(20.0, -1, -1, /*autoStart=*/false);  // base block only, PLL off
synth.setFrequency(1900.0);                      // the one and only lock cycle
```

## Input validation (changed in 1.1.0)

Every `configureRn_*()` now returns `bool`. An out-of-range argument is
**rejected** (returns `false`, nothing is written or cached). Up to 1.0.1 these
functions silently clamped or substituted a value, so if your code relied on
that, check the return value. Behaviour that differs from 1.0.x:

- **R0 / R1 / R2:** INT, MOD and FRAC outside their valid ranges are rejected
  instead of clamped.
- **R3:** the dither magnitude must be exactly 15, 7, 3 or 1 (it used to be
  rounded down to the nearest encodable value).
- **R4:** reserved MUXOUT values 5 and 6, and any other out-of-range field,
  are rejected.
- **R5 / R6:** CDAC, VCO amplitude and VCO band-select above their field width
  are rejected (the amplitude used to fall back to 47).
- **`setFrequencyManual()`:** MOD and FRAC are range-checked in integer mode
  too, since R1 and R2 are always written.

Valid inputs produce exactly the same register words as 1.0.1.

## AVR notes

`double` is 32-bit on AVR, so `setFrequency(double)` converts MHz→Hz with
roughly 100 Hz resolution at 1.7 GHz. Use `setFrequencyHz()` for an exact
value. All divider maths after that point is exact on every platform.

## Wiring (ESP32 VSPI defaults)

| ADRF6655 pin | ESP32 GPIO |
|---|---|
| DATA | 23 (MOSI) |
| CLK | 18 (SCK) |
| LE | any GPIO (constructor argument) |
| MUXOUT | any GPIO (optional, for lock-detect) |

Power the ADRF6655 chip using an external 5V supply

Also, connect the ADRF6655 IP3SET pin to 3.3V supply

## Quick start

```cpp
#include <ADRF6655.h>

ADRF6655 synth(/*lePin=*/5);

void setup() {
  Serial.begin(115200);
  synth.begin(20.0);          // REFIN = 20 MHz; runs powerOnSequence()
  synth.setFrequency(1740.0); // sets LO to 1740 MHz
  synth.printStatus();
}

void loop() {}
```

## Examples

- **BasicFrequency** — set one fixed LO frequency at startup.
- **FrequencyChirp** — sweep between a min and max LO frequency,
  accounting for SPI/register-write latency and PLL lock time.
- **SerialControl** — type frequencies (or register values) at the
  Serial Monitor and apply them live. Frequencies are parsed as exact
  decimal MHz (e.g. `f 1740.012345`) and the achieved frequency and error
  are printed back. `mod <n>` sets the fixed MOD and `automod on|off`
  toggles the best-fit MOD search.

## Known limitations

- Every retune runs the full deactivate → reload → activate sequence
  (≈100 ms + `lockWaitMs`); sweeps are stepped with gaps, not phase-continuous.
- `setFrequency*()` uses `fLO = fPFD × (INT + FRAC/MOD)` for both LO-divider
  settings. If you rely on the ÷3 range (1050–1530 MHz), confirm the output
  frequency on a spectrum analyzer.

## Tests

Host-side tests need only `g++` and `make` (no Arduino toolchain, no hardware):

```
make -C extras/test
```

`golden` diffs every SPI word against output captured from v1.0.1, proving
default behaviour is unchanged; `api` covers the v1.1.0 additions. CI also
runs `arduino-lint` and compiles all examples for ESP32 (and AVR, informational).

## Bit-position notes

A few register fields in the datasheet's per-register figures don't
match its own plain-text prose or the master overview table (Figure
69). This driver follows whichever source is most explicit:

- **R5**: `MBE = DB7`, `PLEN = DB6`, `LDIV = DB5`, `LXL = DB4`,
  `LDRV = DB3` — confirmed by explicit datasheet prose ("mixer bias
  enable address (DB7)", "PLL enable address (DB6)").
- **R6**: `CPEN=DB20, L3EN=DB19, LVEN=DB18, VCOEN=DB17, VCOSW=DB16,
  VC[5:0]=DB15:DB10, VBSRC=DB9, VBS[5:0]=DB8:DB3` — confirmed against
  Figure 69 and the prose citing "DB9" and "DB8 to DB3" explicitly.
- **R1/R2**: `MOD`/`FRAC` fields span `DB13:DB3` (11 bits, `<<3` shift).

If you ever see unexpected register values in `printStatus()`, this
section is the first place to check.
