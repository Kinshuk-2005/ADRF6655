# Changelog

## 1.1.0

Default behaviour is unchanged: the SPI words emitted by `begin()`,
`setFrequency()`, `setFrequencyManual()` and the `configure*` functions are
byte-identical to 1.0.1 for all valid inputs (see `extras/test`).

### Fixed
- `setFrequency()`: INT is no longer computed through a `uint8_t` cast. With a
  small PFD (e.g. `REF_PATH_QUARTER`) an INT above 255 used to wrap into the
  valid range and be programmed silently; it is now range-checked as 32-bit
  and rejected.
- Removed `ESP8266` from the ESP32 `SPI.begin(sck, miso, mosi, ss)` guard
  (that overload does not exist there).
- `setFrequencyManual()` now range-checks MOD and FRAC in integer mode too,
  since R1/R2 are always written.
- `getFrequency()` ignores FRAC/MOD when DM=1 (integer mode), matching the
  hardware.
- `printStatus()`: `snprintf` buffer enlarged (format-truncation warning).

### Changed
- **`configureR0…R7` now return `bool` and reject out-of-range arguments**
  instead of silently clamping/substituting (R6 amplitude used to fall back to
  47; R3 magnitude rounded down; R4 reserved MUXOUT values 5/6 are rejected).
- Frequency maths is exact 64-bit integer (PFD stored in Hz). The result is
  exact on AVR, where `double` is only 32-bit.

### Added
- `setFrequencyHz(uint64_t)`, `getFrequencyHz()`
- `getLastFrequencyErrorHz()` — achieved − requested, in Hz
- `setModulus()` and opt-in `setAutoModulus()` (best-fit MOD search)
- `begin(..., autoStart=false)` / `powerOnSequence(false)` to skip the
  1740 MHz placeholder lock (~120 ms)
- `printStatus()` decodes R3, R4, R6 and shows the last frequency error
- SerialControl: exact decimal MHz parsing (no `toFloat()` loss), `mod`,
  `automod`, achieved-frequency/error readout
- Host tests (`extras/test`), GitHub Actions CI (lint + tests + compile)

## 1.0.1
- README: ADRF6655 IP3SET connection details; power-supply instructions.

## 1.0.0
- Initial release.
