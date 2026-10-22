# AntExpert 0.1.2 code review — 2026-10-22

Reviewed `antenna_expert` at `fa34f523d429e6f9f19d789b3dd7b1e24c9fef66`.
This records the original review, before repairs. See the
[repair and validation record](2026-10-22-repairs.md) for the implemented fixes.
The product version and calibration format remain unchanged.

## Scope and validation

The pass covered the first-party measurement/DSP and calibration paths, ANT band,
Multi and cable workflows, VNA rendering and menus, persistent storage, USB shell,
RTOS integration and H4 drivers, linker layout, build tools and CI configuration.
The focus was on reachable H4 behavior and failure paths. Retained F072 sources
and the entire vendored ChibiOS/FatFs implementations were not independently
audited line by line. A passing build is not evidence that all hardware races or
measurement errors have been eliminated.

WSL became available during the review. Results:

- Existing `make test`: passed (17 C suites and 17 Python harnesses).
- Existing suite with AddressSanitizer/UndefinedBehaviorSanitizer: passed.
- Isolated ARM build: passed with `make TARGET=F303 USE_LTO=yes -j8`, ARM GCC
  14.2.1. Existing CMSIS stack-pointer-clobber and LTO assembler-listing warnings
  remain; this was not a warning-free build.
- Eleven additional diagnostic cases reproduced twelve distinct defects below
  (the ordinary-CAL case covers both a failed capture and bandwidth truncation).
- Six flash-size experiments built successfully in an isolated source export.
- No flash operation or new hardware measurement was performed for this review.

The original failure probes were converted into
[regression tests](../../tests/unit/test_review_fixes.py), now part of `make test`.
They extract production function bodies and supply small host stubs; related SD,
USB and RTOS tests exercise the real modules/callers. Run the converted cases in
WSL with:

```sh
python3 tests/unit/test_review_fixes.py
```

## Reproduced defects

These descriptions record the pre-fix behavior. These were current-tree findings, not claims
that each was introduced in 0.1.2. Fix the measurement errors and bounds failures
before adding more features. P2 means a functional defect to address in a coming
fix release; the narrower bandwidth issue is P3.

### R01 — P2: Smith trace coordinates distort impedance

Location: [traces.c](../../src/ui/draw/traces.c), `trace_into_index`, lines 365–386.

The Smith path transforms reflection coefficient `g` to `2*g/(1+|g|^2)` before
plotting it. A Smith chart plots the reflection coefficient itself. With scale 1,
radius 120 pixels, and a 100-ohm load referenced to 50 ohms, `g=1/3`: the correct
offset from the center is 40 pixels, but the production function produces 72.
This explains an expanded-looking trace, independently of chart size. It also
makes the trace disagree with the numeric impedance and Smith grid.

Repair: use the normal Cartesian reflection-coefficient mapping, retaining bounds
clipping. Test matched, open, short, 25/100-ohm and reactive loads at multiple
scales, and verify Smith/admittance orientation on hardware.

### R02 — P2: Standard VNA S21 series/shunt impedance formulas are incorrect

Location: [plot_internal.h](../../include/ui/draw/plot_internal.h),
`get_s21_r/get_s21_x`, lines 195–202; callers in
[traces.c](../../src/ui/draw/traces.c).

An S21 value of `0.5+j0` should give 100 ohms in the series fixture and 25 ohms in
the shunt fixture; the functions return 300 and 75 ohms. A series `+j50` example
returns `+j100`. This is separate from the corrected S11 helpers used by ANT.

For these existing callers, with `l=re*re+im*im`, the shared real result should
be `z*(re-l)/l` and imaginary result `-z*im/l`. Define singular behavior explicitly
and add analytic series/shunt reference cases, including complex values.

### R03 — P2: Inductance/capacitance conversion uses a selector as a point index

Location: [plot_internal.h](../../include/ui/draw/plot_internal.h), `get_w`, line 204.

`get_sweep_frequency(ST_START+i)` selects START/STOP/CENTER/etc.; it does not return
the frequency of point `i`. At point 50 on a 14.000–14.350 MHz sweep it returns
zero instead of 14.175 MHz. L/C conversions therefore use incorrect frequencies
or divide by zero. This affects standard VNA component displays, including Smith
R/L/C formatting, rather than ANT's resistance/signed-reactance line.

Repair: use `get_frequency(i)` with appropriate zero-frequency behavior. Test
start, interior, last and CW points against known inductors/capacitors.

### R04 — P2: Ordinary VNA CAL accepts failed captures

Location: [calibration.c](../../src/processing/calibration.c), `cal_collect`,
lines 200–226.

The routine sets the collected flag before measurement, ignores the boolean
result from `app_measurement_sweep`, and copies the buffer even after failure.
The injected-failure test returns false yet OPEN is marked complete and stale
samples are installed. A real capture timeout can consequently produce a saved
calibration made from old or partially refreshed measurements.

Repair: only commit samples, grid metadata and status after a complete valid
capture; propagate failure to the menu/shell and restore temporary settings.
Test failures before the first sample and part-way through, plus retry and DONE.
The ANT walkthrough already checks capture success and is not the failing path.

### R05 — P3: Ordinary CAL truncates the saved bandwidth setting

Location: [calibration.c](../../src/processing/calibration.c), line 209.

The configuration bandwidth is 16-bit but `cal_collect` saves it in `uint8_t`.
The supported setting 500 becomes 244 when restored. Use the field's correct
width; cover settings above 255 on both success and failure.

### R06 — P2: A one-point scan reaches integer division by zero

Locations: [shell_commands.c](../../src/sys/shell_commands.c), `cmd_scan`, lines
227–234; [sweep.c](../../src/rf/sweep.c), `app_measurement_set_frequencies`,
lines 931–937.

`scan 14000000 14350000 1` is accepted, then the frequency generator divides by
`points-1`. The sanitizer reproduction confirms division by zero. A one-row SD
Touchstone import also installs a one-point sweep. Rectangular trace indexing
independently divides by `sweep_points-1`. Parsing into a `uint16_t` before bounds
validation additionally permits larger integers to wrap into accepted counts.

Repair: establish one common point-count contract and validate before narrowing.
Either reject counts below two everywhere or deliberately support single-point
sweeps in every consumer. Test 0, 1, 2, 401, 402 and integer-wrap inputs through
the shell/import/UI callers. Do not infer a guaranteed hardware reset from host
UB: the actual Cortex-M divide-by-zero behavior depends on its trap setting.

### R07 — P2: CW rectangular grids take a modulo by zero

Location: [grid.c](../../src/ui/draw/grid.c), `update_grid/rectangular_grid_x`,
lines 244–280; rendering caller in [plot.c](../../src/ui/draw/plot.c), line 756.

Setting equal start/stop frequencies sets `grid_width=0`. Rendering an interior
vertical-grid pixel then performs `% grid_width`. The production functions fail
the sanitizer probe with a 14 MHz CW sweep. This is distinct from R06: an ordinary
multi-point CW sweep has a valid point count and still triggers this grid issue.

Repair: define the CW grid appearance and explicitly handle zero span/width.
Test CW and tiny nonzero spans with the rectangular grid actually rendered.

### R08 — P2: Marker ALL OFF permits a negative array index

Locations: [runtime_entry.c](../../src/runtime/runtime_entry.c),
`get_marker_frequency`, line 540; [menu_marker.c](../../src/ui/menus/menu_marker.c),
operation/search callbacks, lines 55–95; [legacy.c](../../src/rf/legacy.c), LC match.

ALL OFF sets `active_marker=-1`. Operations and search remain reachable; the
frequency helper reads `markers[-1]` before callers can check its returned value.
The sanitizer probe confirms the invalid access. Some callers also index markers
directly, so guarding the helper alone is insufficient.

Repair: validate selected marker and enabled state at action boundaries and in
shared accessors; disable or harmlessly ignore actions without a selection. Test
ALL OFF followed by frequency operations, search and LC match.

### R09 — P2: An empty, unterminated quoted command reads beyond the string

Location: [common.c](../../src/core/common.c), `_strpbrk/parse_line`, lines 191–241.

After an opening quote at the end of input, `_strpbrk` receives a pointer to NUL
and advances before checking termination. ASan reports an out-of-bounds read for
a buffer containing just `"`. USB shell and imported command text share this
parser. Repair the initial-NUL and quoted-token advancement logic; test empty,
closed-empty, unterminated, adjacent and trailing quoted arguments and argument
limits. The rejection must leave the command buffer and prompt usable.

### R10 — P2: Leftward measurement search invents a crossing at the edge

Location: [analysis.c](../../src/rf/analysis.c), `measure_search_value`, lines 88–123.

A leftward search exits at index zero without establishing that a crossing was
found, then interpolates anyway. A constant trace at 1, searching for 0, reports
2 MHz instead of no result in the reproduction. Filter/LC analysis can therefore
display a fabricated bandwidth or resonance boundary.

Repair: distinguish found/not-found/end-point cases before interpolation. Test
flat traces, no crossing on either side, crossings exactly at endpoints, and
normal interior crossings. ANT's separate SWR bandwidth calculation is not this
function. Also inspect `search_peak_value`'s edge return, which can leave its
output index unset (source-review follow-up).

### R11 — P2: SD CAL import retains the previous ANT calibration tag

Location: [menu_storage.c](../../src/ui/menus/menu_storage.c), `load_cal`, lines 291–305.

The ordinary CAL file magic is read into a local variable, but only the bytes
after it are copied into `current_props`. With an ANT1 profile active, the import
reports success while leaving ANT1 as the active magic. Uniform-grid calibration
data can then be interpreted as band-distributed ANT data. The reproduction
verifies this stale tag. Conversely, saving active ANT1 properties creates a CAL
file that this loader rejects because it only accepts ordinary properties magic.

Repair: decide explicit SD export/import support for ANT profiles; validate the
complete incoming properties and commit magic/data together. Check exact byte
counts, checksum, field bounds and failure rollback. Test ANT-to-ordinary import,
ordinary round trip, rejected ANT files, truncated files and invalid point counts.
Do not let SD import bypass the walkthrough-only reserved-slot write policy.

### R12 — P2: Renormalization assumes a 50-ohm calibration standard

Location: [pipeline.c](../../src/rf/pipeline.c), lines 46–76.

The UI supports changing calibration LOAD R, but the active conversion hardcodes
50 ohms and skips work when port Z is 50. For a calibration referenced to a
75-ohm load, a measurement of that load has `Gamma=0`; at display Z0=50 it should
become `Gamma=0.2`. It stays zero in the production-function probe. Setting both
load R and port Z to 75 also applies an unnecessary 50-to-75 conversion.

Repair: use the calibration reference consistently, preserving the ordinary
50-ohm case and defining S21 behavior. `SWEEP_USE_RENORMALIZATION` is set elsewhere
but is not consumed by the sweep implementation. USB `cmd_scan` calls the sweep
directly rather than this pipeline, so compare its reference-impedance contract
with displayed results as well. Existing pipeline host tests do not enable this
compile-time feature; new tests must exercise the enabled production branch.

## Additional findings from source inspection

These have concrete source paths but were not reproduced on attached hardware.
Keep them separate from the diagnostic cases above.

| ID / priority | Finding and impact | Repair and verification |
| --- | --- | --- |
| S01 / P2 | `shell_commands.c:272`: a failed `scan` avoids the generation increment but still sends the full measured buffer as if the scan completed. Data may be stale or partially refreshed. | Fail the command explicitly, restore state, and define a host-compatible failure response. Inject partial capture failure and verify no successful-looking result escapes. |
| S02 / P2 | `shell_service.c:93–116`: a connected host that stops reading can make `shell_io_write` retry forever; every timeout feeds the watchdog. Deferred output runs on the sweep/UI owner, so a stalled capture/scan transfer can stop interaction. | Add an overall deadline/cancellation path, then recover the prompt and measurement state. Test DTR held high with no reads, partial reads and disconnect/reconnect. Also audit ownership after the five-minute deferred-command timeout. |
| S03 / P2 | `runtime_entry.c:812` initializes a six-node event queue, but there is no production call to `event_bus_dispatch`. It fills and subsequent events are dropped. `config_service_attach_event_bus` also has no production caller, so configuration-change notifications never reach their save callback. Explicit saves still work. | Decide which events are actually needed and whether settings are explicitly saved or automatically persisted. Either simplify to direct calls or integrate dispatch on the correct owner thread; do not silently introduce writes on every setting change. Add a runtime integration test that exceeds queue depth. |
| S04 / P2 | `menu_storage.c:203,278` passes an SD filename directly as the `lcd_printf` format string. A filename containing `%s` or another conversion makes formatting consume arguments that were not supplied. | Print with a fixed `"%s"` format; test percent signs and maximum-length filenames. |
| S05 / P2 | SD save callbacks check FatFs status but not returned byte counts; `ui_save_file:454` ignores `f_close` failure. A full card or final flush failure can be reported as a successful calibration/sweep save. | Check every requested/actual write count and close result. Inject short writes and final flush failure; preserve a useful failure message. |
| S06 / P2 | `load_snp_data:99–143` only terminates records on CR, ignores the Touchstone options line, and recreates a uniform grid from first/last frequency. LF-only files, non-Hz/non-RI formats, or nonuniform input do not receive correct interpretation. | Either parse supported options/line endings/grids, or explicitly reject unsupported files rather than silently misread them. Test these formats and a final line without a newline. |

### Integration and robustness follow-ups

- **Do not simply switch on event dispatch to fix S03.** Measurement events carry
  a pointer to a local stack mask (`measurement.c:76`); queued payloads need stable
  storage or no payload. `event_bus_enqueue` calls `chMBPostI` after releasing its
  ISR lock. UI progress callbacks and deferred-shell callbacks must run on the
  intended owner and respect ANT rendering. These are latent integration hazards
  while the queue is undrained, not proof of a currently occurring ISR failure.
- **Configuration validation:** checksums and magic do not establish that point
  counts, trace/marker indices, or bandwidths are in range. Validate imported and
  recalled properties before use; cover corrupted-but-checksummed payloads.
  The intentionally unchanged private ANT1 format and the documented 0.1.2 V/U
  recalibration requirement are not being reported as defects.
- **Hardware failure recovery:** H4 I2C polling loops in `boards/STM32F303/i2c_v2.c`
  have unbounded waits. A higher-level PLL timeout does not bound a stuck I2C
  transaction. Test bus errors/NACK/stuck BUSY with bounded failure handling.
- **Latent infrastructure:** `scheduler.c` releases its lock between reserving
  and assigning a thread slot, allowing reuse/races if used concurrently. Current
  runtime starts its threads directly. The DMA4 handler clears other DMA1 flags;
  review that before enabling more DMA interrupts. Neither is a demonstrated
  regression in the present hardware configuration.
- **Maintenance:** `CONTRIBUTING.md` still directs contributors to `main` and
  requires F072 builds, contrary to this fork's H4-only workflow. Firmware CI
  masks `dfu-suffix --check` failures with `|| true`; the release workflow does
  not. Make artifact validation failures visible in CI.

## Firmware-space log

The isolated archive build has **116,500 bytes** of flash load against a
**116,736-byte (114 KiB)** firmware region: **236 bytes free**. The previously
flashed 0.1.2 binary is 116,504 bytes, with 232 free. The archive uses provenance
`unknown` instead of the flashed build's longer Git provenance; these are not
claimed to be byte-identical images. All experiment deltas below use the same
archive baseline and toolchain.

The linker map also leaves only **16 bytes of CCM** (8,176 / 8,192 used) and
**368 bytes at the main-RAM heap boundary**. Statically allocated thread stacks
are already included in RAM use; the latter number is not runtime stack headroom.
Stack-watermark tests on the device are still needed. Compiler stack reports
show individual frames up to 408 bytes for `cmd_scan`; those reports are not a
complete worst-case call-chain/interrupt analysis.

### Measured candidates retaining standard VNA functions

Each source experiment prepended `#pragma GCC optimize("Os")` in an isolated
export and rebuilt with LTO. The combined result was separately rebuilt with
`make -B`; these changes were not applied to the working firmware sources.

| Candidate | Image bytes | Saved vs baseline | Free bytes | Decision |
| --- | ---: | ---: | ---: | --- |
| Entire LCD driver optimized for size | 116,240 | 260 | 496 | Promising; LCD already switches to Os part-way through the file. Check draw speed, SPI transfers, capture and flicker. |
| Si5351 driver optimized for size | 116,276 | 224 | 460 | Promising; verify tuning/settling, band transitions and sweep timing on both supported hardware modes. |
| Both preceding changes | 116,016 | **484** | **720** | Best measured candidate; requires hardware validation before adoption. |
| FatFs optimized for size | 116,500 | 0 | 236 | Already uses Os; no gain. |
| ANT math source optimized for size | 116,532 | -32 | 204 | Already has Os; duplicate directive/LTO layout did not help. Reject this experiment. |
| Function alignment 2 instead of 4, full rebuild | 116,500 | 0 | 236 | No measured gain. |

The two driver savings happened to add exactly in this build; that is not a
general guarantee for future changes under LTO. Re-measure after bug fixes and
with final release provenance.

### Further options to measure, with constraints

1. **Remove the incorrect Smith transform as part of R01.** Correctness comes
   first; sharing the existing Cartesian path may also remove arithmetic/code.
   No isolated size delta has been measured for that repair yet.
2. **Compress the remaining 7x11 font.** `x7x11b_bits` occupies 1,155 flash bytes.
   The numeric font is already compressed (offsets 46, row table 260, packed data
   372 bytes). Measure compressed bytes plus decoder/index cost, verify every
   glyph pixel, and check rendering time and RAM. Do not count unlinked font
   source files as savings; section GC already removes unused data.
3. **Reduce avoidable software-double arithmetic in UI formatting.** Linked
   helpers include double multiply (596 bytes), add/subtract (overlapping code,
   roughly 630 bytes), and conversions. Integer Hz/fixed-point formatting might
   remove some dependencies while preserving frequency precision. Variadic float
   promotion and other users may keep them linked; do not promise the sum of
   symbol sizes as savings, and do not replace calculations blindly with float.
4. **Simplify unused event/scheduler infrastructure after resolving S03.** Some
   unused functions are already removed by LTO, but live queue publication,
   subscriptions and storage still cost code/RAM. Measure the actual linked
   difference while preserving ownership, USB behavior and configuration saves.
5. **Deduplicate UI formatting/redraw paths selectively.** `draw_all` is large
   after LTO inlining; size alone does not show waste. Shared bounded-format or
   rendering helpers may save bytes, but changes must not reintroduce flicker or
   increase stack use. Measure before accepting a refactor.

The quarter-wave sine table is already 1,028 bytes. Further reduction needs an
accuracy/performance study for FFT and signal processing, not just fewer entries.
Removing SD, standard VNA features, or enlarging firmware into calibration space
does not satisfy the requirement to preserve normal functions and saved slots.

## Suggested repair sequence and remaining checks

1. Fix R01–R04 and R12 with analytic measurement references; exercise both normal
   and fast-math compiler modes. Include R05 in the ordinary-CAL repair.
2. Fix R06–R09 and R11 with actual entry-point bounds/failure tests, then R10 and
   USB failure recovery (S01–S02).
3. Resolve event ownership/persistence design (S03) and SD errors/import behavior
   (S04–S06), followed by bounded driver failures and documentation maintenance.
4. Re-run host/sanitizer tests and WSL H4 builds, then test candidate size settings
   on hardware: battery-only boot/unplug, sweep and paused modes, roller/touch,
   standard CAL and ANT dual CAL/save/recall, CW, known Smith loads, NanoVNA-Saver,
   slow/non-reading USB host, and SD failure behavior if that feature is used.

Local evidence is retained in
`C:\1_HAM\NanoVNA-X-builds\audit-2026-10-22`: `tests.log`, `sanitizers.log`,
`reproductions.log`, `build-baseline.log`, `baseline.bin/.elf/.map`,
`symbol-sizes.txt`, `space-results.json`, `combined-space-results.json`, and the
isolated experiment scripts/logs. No automatic commit, push or firmware update
is part of this review.
