# ZeeTK hardware support (issue #10)

Adds a selectable ZeeTK RF profile for NanoVNA-H rev3.7 and NanoVNA-H4 rev4.4
using NE602A mixers and the SJWCH5351 signal generator. Earlier hardware
profiles and their saved configuration numbers remain unchanged. Profile
changes invalidate the synthesizer cache; harmonic band changes now update
drive strength even when the multisynth divider stays the same.

The profile's PLL multipliers, 32 kHz / 130 MHz boundaries, harmonic ratios,
drive strengths and codec gains come from [hugen79/NanoVNA-H 1.2.50](https://github.com/hugen79/NanoVNA-H/blob/816495e/si5351.c).
NanoVNA-X already supports the 600 Hz minimum and a configurable approximately
300 MHz harmonic threshold, so those global defaults are unchanged. Existing
NanoVNA-X RF settling constants are retained and need confirmation on physical
hardware; the separate runtime fixes do not change those values.

## Build and automated checks

Run in WSL Ubuntu, with GNU Make, host GCC and the Arm GNU toolchain installed:

```sh
make test
make clean TARGET=F303
make TARGET=F303
# Preserve build/H4.{elf,bin,hex} before cleaning for the other board.
make clean TARGET=F072
make TARGET=F072 USE_LTO=yes
```

The synthesizer test decodes actual I2C register writes to check RF and audio
frequencies, drive currents and ADC gain around band boundaries. It also
switches from a longer legacy table at 2.7 GHz to catch stale cached indices.

Normal host tests and the H/H4 RF tests with ASan/UBSan pass independently.
Clean F303 and F072 (USE_LTO=yes) builds pass in WSL with Arm GCC 14.2.1.
The full sanitizer suite on this RF-only branch still detects the upstream
negative-exponent signed shift in `vna_sqrtf`; the separate runtime reliability
PR fixes it, and the combined firmware passes the full sanitizer suite.

## RF correctness and user workflow

The frequency cache stores requested Hz, including at low frequencies where
the driver scales the PLL input. Harmonic lookup stays within the selected
table even with a custom threshold. Switching profiles invalidates cached
PLL/gain state, and band changes refresh drive strength even if the divider
does not change. Saved configuration is loaded before generator initialization.

Choose System -> Device -> More -> Mode -> ZeeTK, then SAVE MODE immediately
below MODE. This saves device configuration, not calibration slots. Recalibrate
with CAL POWER set to AUTO after changing the RF profile.

## Validation

The tester identified a NanoVNA-H4 rev4.4 / ZeeTK, originally on vendor 1.2.50,
and reported successful hardware tests on 2026-10-12. Save Mode persistence
was checked across restart and after Clear Config. The tested device image
also contains the separately proposed runtime/USB reliability fixes; this RF
branch alone has not been flashed. Host register-level tests cover both H/H4,
but no physical NanoVNA-H rev3.7 or exhaustive RF accuracy claim is made.

The final integration check confirmed normal display sweeping and a successful
NanoVNA-Saver sweep on COM13. Twelve USB channel reads across three serial
sessions each returned all 401 points while continuous sweeping was active.

The separate reliability PR addresses capture-task starvation, timer rollover,
ADC readiness, USB snapshots and CDC session handling. Antenna Mode is not
included. Existing RF settling constants and the larger vendor synthesizer
arithmetic rewrite remain deferred pending targeted bench measurements.
