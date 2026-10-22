# VNA correctness and reliability follow-up

This branch is based directly on upstream `main` at `dddbfc7`. It ports the general
fixes validated in NanoVNA-AntExpert 0.1.3 without adding ANT UI, band plans,
reserved ANT calibration slots, hardware-selection onboarding, product branding,
or an upstream version bump. Both F303/H4 and F072/H remain supported.

## Changes

- Smith traces plot the reflection coefficient directly. S21 series/shunt
  impedance uses the correct complex formulas, and L/C uses each point's actual
  frequency. Searches reject missing crossings and handle exact/edge results.
- Calibration retains previous state on failed/nonfinite captures and restores
  the full bandwidth value. Menu and USB callers report capture failure.
- USB scans reject invalid counts before narrowing, stop on capture failure and
  share S11 renormalization with display sweeps, including non-50-ohm standards.
- Disabled/invalid markers and CW/one-point division cases are guarded. Shell
  parsing rejects unterminated quotes and excess arguments. Ten consecutive
  no-progress USB write waits abort the response; the next command can recover.
  Deferred commands retain their arguments until completion.
- Remove the undrained runtime event queue while preserving direct owner-thread
  calls and explicit Save Config. Optional event-bus users retain the public API;
  ISR posting holds the lock and queued notifications do not retain stack data.
  Scheduler slots stay reserved until joined, including immediate task exit.
- Bound H4 I2C waits and handle NACK/bus/arbitration faults. Each H4 DMA handler
  acknowledges only its channel. LCD/Si5351 size optimization removes no features.
- Validate saved properties/configuration and RTC-restored bounds. Refresh CAL
  export checksums, invalidate the save-slot checksum cache after writes and
  verify programmed properties. Binary SD operations check exact byte counts;
  save-close failures propagate, filenames use fixed formats and TIFF runs have
  bounds checks. Touchstone accepts CR/LF/final lines and validates Hz/S/RI/50-ohm
  options and uniform point spacing; headerless legacy Hz/RI input still works.
- Limit H4 code to 114 KiB: upstream's previous 142 KiB code region overlapped
  the calibration region starting at `0x0801C800`. Saved addresses are unchanged.
  CI no longer suppresses DFU suffix validation failures.

## CAL import compatibility

H4 stages imports in otherwise unused flash above the saved records, starting at
`0x08039000`, before validating and adopting the complete image. Compile-time
checks enforce alignment and flash bounds. Active CAL and all saved slots remain
unchanged on a failed import. This does not change any persisted structure or
slot address and does not require an SD card for normal VNA operation.

H has insufficient unused flash for a complete staging image. It validates the
file size and tag first; a subsequent short read or invalid image resets active
properties to safe defaults and reports an error. It never overwrites saved
slots, so a saved calibration can be recalled afterward. Both paths are tested.
Old CAL exports with stale checksums now fail validation.

## Standalone validation

WSL Ubuntu, Arm GNU Toolchain 14.2.1, LTO enabled:

- `make test`: all 11 C suites and six Python harnesses pass.
- New compiled Python regression harnesses pass ASan/UBSan; the review harness
  also passes with `-ffast-math`. The first ten standalone C suites pass
  ASan/UBSan. The last, accuracy analysis, exposes the pre-existing negative-shift
  bug in `vna_sqrtf`, already fixed in runtime PR #11.
- `make TARGET=F303 USE_LTO=yes BUILDDIR=build-h4 -j8`: passes; firmware is
  104,180 bytes, leaving 12,556 bytes before calibration storage.
- `make TARGET=F072 USE_LTO=yes BUILDDIR=build-h -j8`: passes; firmware is
  98,248 bytes, leaving 2,104 bytes before calibration storage.
- Existing CMSIS stack-pointer-clobber and LTO listing warnings remain.

## Integration with existing PRs

[PR #11](https://github.com/momentics/NanoVNA-X/pull/11) supplies the earlier
ChibiOS scheduling, USB snapshot and startup fixes.
[PR #12](https://github.com/momentics/NanoVNA-X/pull/12) supplies ZeeTK RF support.
Neither PR's commits are included in this branch. A separate local integration
checkout combines all three for tests.

Full normal and ASan/UBSan suites pass in that integration checkout, including
the accuracy analysis fixed by #11. Both combined WSL builds also pass: H4 is 105,044 bytes (11,692 bytes free)
and H is 98,992 bytes (1,360 bytes free), within their respective code boundaries.

PR #12 merges without conflict. Combining with #11 requires retaining both sides
of three overlapping changes:

1. H4 DMA handlers: keep #11's RTOS prologue/epilogue and this branch's
   channel-specific flag masks.
2. `cmd_scan`: keep this branch's failed-capture handling and renormalization,
   incrementing generation only after successful capture.
3. SD Touchstone: use this branch's parser as `load_snp_data`, inside #11's
   measurement ownership/publication wrapper. Its host fixture needs
   `FREQUENCY_MIN` and the case-insensitive string helper for option validation.

The parser-only test accepts either the direct or wrapped loader arrangement.
Existing PRs are unchanged. Merging #11 first is recommended; this branch can
then be rebased using the tested resolutions above.

## Hardware evidence and limits

Shared fixes were flashed as AntExpert 0.1.3 to an H4/ZeeTK. Full firmware readback
matched and saved settings/calibration were unchanged byte-for-byte. USB reported
the expected version and returned two finite 101-point S11 snapshots. The user
confirmed ANT/VNA sweeps, touch/roller, unplugging USB and battery restart, with no
blanking, freezes or resets.

This standalone upstream image was not flashed. No physical F072, exhaustive RF
accuracy/settling, or SD-card fault-injection hardware test is claimed.
