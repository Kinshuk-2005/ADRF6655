# ADRF6655 Arduino Library

ESP32 / Arduino SPI driver for the Analog Devices **ADRF6655** broadband
up/downconverting mixer with integrated fractional-N PLL and VCO.

## Architecture

- **`writeRegister(address, bitmask)`** — the single low-level function
  that sends a 24-bit word over SPI and latches it with LE. Every other
  function funnels through this one.
- **`configureR0_IntegerDivide()` … `configureR7_ExternalVCO()`** — one
  function per register. Each takes every field of that register as a
  named parameter, builds the bitmask, and calls `writeRegister()`.
- **`setFrequency(loFreqMHz)`** — computes `INT`, `MOD`, `FRAC` from a
  target frequency, decides integer vs. fractional mode automatically,
  and runs the full deactivate → reload → activate sequence.
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

`setFrequency()` and `setFrequencyManual()` both run this sequence
internally — you don't need to call the steps yourself. Plan for
roughly **100 ms + lockWaitMs** of latency on every retune; the deep
deactivate/reactivate cycle is part of the procedure, not an
optional safety margin.

Power-up (`begin()` → `powerOnSequence()`) additionally writes the
static blocks before the frequency load:

```
R7 → R6 → R5(PLEN=0) → R4 → R3   (base block init, PLL dark)
R2 → R1 → wait → R5(PLEN=1) → wait → R0   (frequency load + activation)
```

## Integer vs. fractional mode

Register 0's `DM` bit changes which `INT` range is valid, and makes
`MOD`/`FRAC` don't-cares to the divider hardware:

| Mode | DM | INT range | MOD / FRAC |
|---|---|---|---|
| Fractional | 0 | 24–119 | both used |
| Integer | 1 | **21–123** (wider) | ignored by hardware |

`setFrequency()` picks integer mode automatically whenever the target
frequency divides the PFD frequency with zero remainder (`FRAC == 0`),
and validates `INT` against the correct range for whichever mode gets
selected. `setFrequencyManual()` validates the same way for whichever
mode you pass in explicitly.

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
  Serial Monitor and apply them live.

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
