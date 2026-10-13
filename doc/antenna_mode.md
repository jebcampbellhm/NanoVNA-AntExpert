# H4 Antenna Mode — implementation and validation history

For current operating instructions, see [the illustrated AntExpert guide](antenna_expert.md).
Dated build results below are historical checkpoints, not the latest build status.

## Release notes draft

Add an H4-only ANTENNA entry to the root menu. It opens a 480 x 320 antenna
screen with large SWR and frequency labels, a band-limited SWR graph, band
presets, saved favorites, and fixed-frequency live tuning. The ordinary VNA
screen remains available through VNA in the upper-right corner.

Fixed-frequency tuning also fixes marker remapping for zero-width sweeps and
spans narrower than one hertz per point. Markers below a new sweep start now
clamp to the start instead of wrapping to the end.

## Use

- **BANDS:** choose HF+6, V/UHF, or FAV. PREV/NEXT page through six large tiles.
  Each tile shows wavelength on the left and center frequency rounded to MHz
  on the right, using the same large font and no approximation prefix.
  A single 60 m tile covers all five channels, with numbered center guides on
  the sweep graph. The sweep also measures the gaps between channels.
  Tap **PTS** at the upper right to cycle 101, 201, and 401 points. The default
  is 101 after power-on; the selection carries between bands during the session.
  The band screen shows its active point count beside the band name.
- **FAV + / FAV \*:** toggle and immediately save the current preset as a favorite.
  This uses the existing configuration save operation, which also saves other
  current device configuration settings. A failed save restores the old favorites.
- **SWEEP:** toggle continuous measurement and pause, preserving the last trace.
  The running button is filled and says RUNNING; the paused button is outlined
  and says PAUSED. From TUNE it returns to a running full-band sweep; if a USB
  command changed the range, it reapplies the selected preset.
- **Roller left/right:** cycle backward/forward through favorite bands in band
  order, wrapping at either end. Selecting a favorite leaves the picker
  and starts a full-band sweep. An empty list does nothing; a single favorite
  stays selected. In TUNE, rotation instead adjusts frequency by the displayed
  step. Pressing the roller still opens/closes the band picker.
- **TUNE:** start at the lowest measured SWR frequency, or the band center before
  a complete sweep is available. Use the roller or large frequency touch buttons
  to move by 1/200 of the preset width (minimum 10 Hz), clamped to that preset.
  The instrument measures 21 points at that same frequency and displays the middle
  sample after each completed sweep. This is continuous updating, not instantaneous
  feedback; RF bandwidth and settling settings still determine update speed.
- **CAL:** open the protected HF ANT / V/U ANT walkthrough. Capture OPEN, SHORT
  and 50-ohm LOAD for both selected profiles, then SAVE to internal slots 5/6.
  See the [current guide](antenna_expert.md#2-calibrate-hf-and-vhfuhf-together).
- **VNA:** restore the prior frequency range, point count, frequency/time-domain
  selection, reference impedance, and running/paused state. Calibration changes
  made while in Antenna Mode remain available.
- **Power-on mode:** entering ANTENNA saves Antenna as the startup mode; leaving
  with VNA saves VNA. Mode restoration runs after normal UI/plot initialization
  and does not write flash. Like favorites, mode changes save the whole device
  configuration. A failed save reports an error and retains the previous saved
  startup choice. The last band sweep or MULTI HF view is restored with fresh
  measurements. TUNE restarts as its band sweep; calibration/picker pages do not
  resume. Single-band startup uses 101 points; no saved view defaults to 20 m.

SWR is referenced to 50 ohms. The graph spans SWR 1 through 5; larger values
clip at the top. The numeric readout displays 99.9+ above its readable range.
Invalid samples are omitted rather than presented as an excellent match.
On 80/75, 40, 30, 20, 17, 15, 12, and 10 m, a thin red bar below the frequency
axis marks the supplied chart's Extra-class CW/RTTY/data region. It runs from
the band start to 3.600, 7.125, 10.150, 14.150, 18.110, 21.200, 24.930, or
28.300 MHz respectively. The bar uses the stored-trace-A palette color (red
by default), has no text, and does not obscure the SWR trace. It spans all of
30 m. It is hidden in TUNE; no separate region is inferred for the other
presets or from the chart's VHF/UHF operating recommendations.
MIN SWR means the lowest sampled SWR, not necessarily resonance (zero reactance).
The 2:1 width is the contiguous dip containing that minimum, with linear
interpolation between samples at its boundaries. `>=` and `(edge)` indicate
that a sweep edge cuts off the dip; that is a lower bound, not a full bandwidth.
Narrow dips between points can be missed, as with the ordinary VNA sweep.

Calibration status distinguishes applied, interpolated, incomplete S11,
uncalibrated, and outside the saved frequency range. It cannot certify connector
quality, calibration age, or RF accuracy. USB commands or calibration recalls
that change the range, point count, domain, or reference impedance invalidate the
antenna display; tap SWEEP to reapply the preset.

## Preset source and storage

Presets follow the user's `US-BandPlan_3-2024.pdf` (Icom US band-plan chart):
160 m, 80/75 m, a combined 60 m view, 40 m, 30 m, 20 m, 17 m,
15 m, 12 m, 10 m, 6 m, 2 m, 1.25 m, 70 cm, 33 cm, and 23 cm. Each 60 m window
is 2.8 kHz wide, centered at 5.332, 5.348, 5.3585, 5.373, or 5.405 MHz.
The combined 60 m sweep runs from 5.3306 to 5.4064 MHz, with guides numbered
1 through 5 at these centers. These dated measurement presets are not a current
transmission authorization or a license-class filter; the continuous measurement
span includes gaps between the channel windows. Upper-frequency presets do not
imply uniform measurement accuracy.

Favorites and the startup view use the three existing reserved configuration
bytes. The current format has 16 direct favorite bits, five view bits (0..15 for
bands, 16 for MULTI), one ANT/VNA bit and a two-bit `11` tag. Invalid view codes,
including erased `0xff` bytes, fall back to defaults. Legacy `0xA`/`0xB` records
are read and migrated when settings change, preserving favorites and startup
mode; any old 60 m channel favorite maps to the combined entry. Older firmware
cannot read the new encoding and falls back to its default favorites/mode.
No configuration size or calibration-slot layout changes. Uninitialized/cleared
configuration defaults to favorites 40 m, 20 m, 6 m, 2 m and 70 cm; an explicitly
empty list stays empty. Cleared or unrecognized settings start in VNA.
Only user changes of band/view or mode save configuration. Boot restoration,
TUNE frequency steps and automatic MULTI scanning do not save measurements or
write configuration. A failed write restores the prior encoded settings.

The H4's CPU-only sweep cache occupies 1,604 bytes of CCM alongside the existing
measurement buffer. It is written completely before use, does not use DMA, and
requires no heap allocation. The two linked font tables total 2,167 bytes in
flash; large labels reuse the existing font at larger scale. Font compression
is deferred because it does not address working RAM and offers little absolute
flash saving here. No antenna screen is built for the smaller F072 NanoVNA-H.

## Verification and bench checklist

`make test` includes independent SWR/bandwidth/favorites tests, production marker
mapping tests, and production antenna UI tests with fake RF/storage/LCD ports.
The UI tests exercise entry/exit restoration, save failures, tune clamps,
calibration transitions, every preset, and pixel bounds using the real H4 font.
`ANTENNA_PREVIEW_DIR=/path` saves PPM screen previews from that same renderer.

Validation on 2026-10-12 after merging the validated runtime/USB fixes at
`6b89542`: all 14 C suites and six production-function/UI Python
harnesses passed, both normally and with AddressSanitizer/UndefinedBehaviorSanitizer.
Clean WSL builds with ARM GCC 14.2.1 passed for F303 and F072 (the latter with
`USE_LTO=yes`). H4: text 115,252, data 552, bss 48,444 bytes; H: text 97,896,
data 328, bss 15,104 bytes. H4 has no compiler warnings; H retains the existing
LTO assembler-listing and CMSIS stack-pointer-clobber warnings. H4 CCM use is
8,020 of 8,192 bytes, and main-RAM heap space is 400 bytes; this feature adds
no dynamic allocation. Host-rendered previews passed pixel-bound checks and
were visually inspected. Physical-device validation remains outstanding.

The integrated H4 binary SHA-256 is
`fcac6372f6b7887bf71a02e2436aaa188da48c7d0f32a4e7911ccf96b48f8eee`.
This image includes capture-task sleeping, CDC queue preservation, and failed
SD-read rejection from the final ZeeTK validation. The earlier on-device
validation covered those fixes without Antenna Mode; the checklist below still
needs to be completed on this image.

The first hardware trial exposed heavy flicker: each sweep cleared the entire
LCD before drawing text and lines. The follow-up renderer composes complete
480 x 8 strips in the existing SPI buffer and transfers them synchronously,
without clearing the LCD. It adds eight bytes of static state and no framebuffer
or heap allocation. Baseline and strip-rendered previews match pixel-for-pixel;
the UI regression rejects screen clearing and checks repeated-frame transfers.
The user confirmed on hardware that flicker is gone and touch controls remain
responsive. The subsequent combined-channel/menu changes still need device testing.

Follow-up validation: all 14 C suites and six Python harnesses pass normally and
with ASan/UBSan; clean WSL H4 and H builds pass with the same toolchain/options.
All 11 preview images match the previous renderer exactly. H4 text is 115,948,
data 552, bss 48,452 bytes; heap space is 392 bytes and CCM use remains 8,020
bytes. H size is unchanged. H4 has no warnings; H retains the existing toolchain
warnings. The follow-up H4 binary SHA-256 is
`4b27483aa3bdc9ffc71e746e86faec6085f87b36244bde469325aebb0045be5d`.

Combined 60 m and initial two-line band-tile validation: all 14 C suites and six Python
harnesses pass normally and with ASan/UBSan, including all 20 legacy favorite
bits and the five channel-guide positions. Rendered HF/VHF tiles and the 60 m
graph were visually checked. Clean WSL H4 and H builds pass; H4 text is 116,104,
data 552, bss 48,452 bytes (RAM use unchanged), and H size is unchanged.
H4 remains warning-free; H retains the existing toolchain warnings.
The combined-channel H4 binary SHA-256 is
`391f555bb1743ff40e26594ebb793aed3848735bafc2b6a017096bf0b9fd5301`.

Before considering this feature hardware-validated:

Startup-mode/favorites-roller update: all 14 C suites and six Python harnesses
pass normally and with ASan/UBSan. Coverage includes old favorites, mode/favorites
coexistence, both startup paths without boot-time flash writes, save failures,
favorite wraparound, empty/single lists, and selecting from TUNE/the picker.
Clean WSL builds pass for H4 and H (LTO). H4 text is 116,552, data 552, bss
48,452 bytes; RAM use is unchanged. H size is unchanged. Hardware restart and
roller-direction checks remain pending.

1. Enter ANTENNA and verify large labels and all four bottom touch targets.
2. Select several HF and V/UHF bands; check frequency limits and paging.
3. Toggle favorites, reboot, and verify persistence, including an empty list.
4. Calibrate a band with known standards. Check a 50-ohm load near 1:1,
   an open/short at high SWR, and a known mismatch against the ordinary VNA view.
5. Check a real antenna's minimum and 2:1 crossings against the ordinary SWR trace.
6. Enter TUNE; verify frequency touch buttons and roller, limits, responsiveness,
   and return to full-band calibration through CAL.
7. Check out-of-range/interpolated calibration labels and return to VNA from
   both running and paused entry states. Verify prior settings are restored.
8. Exercise USB capture/Saver alongside the display and check for flicker,
   missed touches, watchdog resets, or unexpected measurement stalls.
9. Rotate left/right through favorites, including wraparound, one favorite,
   and an empty list. Check that pushing still opens/closes the picker.
10. Enter Antenna, power-cycle, and verify Antenna opens; leave through VNA,
    power-cycle, and verify VNA opens. Favorites must survive both transitions.

Multiband overview tiles and noncontiguous band-only scanning remain follow-up
work; this first version performs one uniform sweep within the selected preset.

### Calibration storage correction and roller input (2026-10-12)

Earlier H4 size checks used the incorrect 142 KiB linker allocation. Existing
calibration storage actually starts at `0x0801C800`, only 114 KiB from the firmware
base. The 117,648-byte controls build overlapped slot 0 by 912 bytes. Flashing that
image could invalidate slot 0; subsequently saving slot 0 could overwrite code.
The device readback before this correction matched that firmware exactly, so
code corruption was not observed on this device. Other saved slots are not moved.

The linker now enforces the 114 KiB boundary, H4 enables LTO by default, and
compile-time structure-size assertions plus a cross-target layout regression
guard against overlaps. The corrected H4 image is 112,908 bytes, with 3,828 bytes
remaining before calibration storage. Existing slot-0 calibration may need to
be replaced by a fresh calibration; the update does not erase saved slots.

Both roller edges now request input processing, so releases are recorded even
when a screen does not wait for release. Release waits no longer generate a
second push click, held-direction repeat handles clock wrap, and either direction
can select an initially unselected menu without a negative bit shift. Host tests
exercise those production input/navigation paths. Antenna hints now show a roller
icon with BAND FAV or FREQ +/- instead of the fixed 50-ohm label.

All 14 C suites and eight Python checks pass in WSL, including the normal and
ASan/UBSan runs. Clean H4 and H (LTO) builds pass. H4 text/data/bss are
112,356/552/48,432 bytes; H values are 97,928/328/15,104 bytes. The existing
CMSIS stack-pointer clobber warnings remain under LTO. H4 binary SHA-256:
`837f419236c8f4247fa38e38fa8db6d3838b500926c84dd57214a11121efe1ef`.
STM32CubeProgrammer verified the download and started it on device 205431682033.
A readback confirmed all bytes from `0x0801C800` through the end of flash were
unchanged by the update. On-device roller and calibration retesting is pending.

### Guided antenna calibration capacity and original design

H4 has seven 16 KiB internal slots. Each current `properties_t` record is 16,244
bytes, including five complex correction arrays at 401 points. A 101-point save
uses the same record size. The existing SD `.cal` file is the same raw structure:
it supports one uniform range and at most 401 points total. Collection resets when
the range or point count changes; interpolation assumes that uniform grid.

For antenna reflection measurements, only the three complex S11 correction terms
are needed: 24 bytes per point. Across the sixteen presets, this is 38,784 bytes
at 101 points per band or 153,984 bytes at 401, before headers and checksums. One
existing internal slot cannot hold either. The current map also leaves 28 KiB
above the configuration region unused; combining that with a reserved slot could
fit the 101-point payload, but requires a new storage format and careful update
recovery across the intervening configuration page. It cannot fit the 401-point
payload. Do not move or repurpose existing slots without a migration plan.

Recommended first implementation: one versioned SD antenna-calibration container
with a band directory, point counts, frequency limits, RF profile, calibration
settings, correction data and checksums. Load one band's coefficients into the
existing RAM buffer. A logical ANT entry can select this container; reserving the
last internal slot as a cache is optional and must not silently replace user data.
Use a distinct format identifier/extension rather than treating it as a legacy
raw `.cal` file.

The wizard should select bands and 101/401 points, then prompt OPEN, SHORT and
LOAD once each, sweeping all selected bands at each step. Stage raw measurements
on SD, derive corrections, validate the complete container, and commit only on
Finish. Cancellation or incomplete files must not replace the prior calibration.
Existing flash writes lack readback verification, and SD calibration imports need
complete-read/checksum/range validation before publishing data to live state;
address these before adding the new save workflow.

For a future multiband display, interpolate within each calibrated segment and
switch segments for each band. Do not assume interpolation across unmeasured gaps
or harmonic transitions is accurate. Validate with known loads, cable lengths and
comparison sweeps on hardware. No calibration accuracy is established by host tests.

### Two protected ANT profiles (2026-10-12)

The current implementation uses two internal profiles rather than an SD container.
Slot 5 is HF ANT (160m through 10m); slot 6 is V/U ANT (6m, 2m, 1.25m, 70cm,
33cm and 23cm). Each has 401 total points on an immutable, versioned grid. Counts
by band are 21/51/9/31/6/36/11/46/11/179 for HF and 61/91/41/101/46/61 for V/U.
The existing property record size and slot addresses are unchanged. A distinct
ANT1 magic prevents old firmware from interpreting these as uniform calibrations.
This development-grid revision retains ANT1 by request; existing HF profiles must
be recalibrated manually because their frequency allocation has changed.
Correction covers the band segments only; gaps and frequencies outside the profile
use identity correction. It is not a continuous broadband calibration. Coefficients
are never blended across a harmonic-mode boundary. RF mode, oscillator frequency,
IF offset, harmonic threshold and calibration drive level are recorded/checked.

ANTENNA > CAL shows CAPTURE/KEEP toggles for both profiles. Missing, legacy or
incompatible profiles are automatically included. Existing matching profiles may
be kept. START prompts OPEN, then SHORT, then a 50-ohm LOAD at the cable end.
CAPTURE measures both selected profiles before advancing to the next connection.
SAVE replaces only the selected profiles, and checks flash readback. The first
save replaces any pre-existing ordinary records in slots 5/6. ANT mode selects
the appropriate saved coefficients automatically when switching bands.

Temporary raw captures use six page-aligned 4 KiB planes from `0x08039000`, in
previously unused H4 flash. No SD card or second in-RAM property record is needed.
Cancel before SAVE leaves both saved profiles unchanged. The two final slot writes
are individually verified, not a power-loss-atomic pair: interruption during SAVE
can leave one updated slot and one previous/invalid slot. Retry a reported save
failure before leaving; incomplete records are rejected by checksum. Captures
do not resume across power cycles. Legacy SD `.cal` import does not yet restore
ANT profiles; use an entire flash backup until a versioned export/import is added.

Normal SAVE CAL offers only slots 0–4. Ordinary saves, USB save commands and
autosave cannot write slots 5/6; Clear Config erases every calibration slot, including both ANT profiles. Native
H/F072 retains its ordinary slot behavior. Full hardware validation remains
necessary, especially at 33cm/23cm and across harmonic transitions.

MODE now opens H4 hardware choices with a left-side explanation and SAVE CONFIG
reminder. Configuration bit 15 records explicit hardware-mode confirmation without
changing configuration size. Missing confirmation opens this screen after UI/plot
initialization and disables applied sweep correction. It does not erase settings
or saved calibration. Save failure does not set confirmation, and RTC backup cannot
override it. Selecting another mode clears confirmation and active calibration;
recalibrate after changing hardware mode. Existing legacy calibrations do not have
the RF compatibility metadata of ANT profiles and should only be recalled deliberately
when the hardware/settings are known to match.

Validation: all 14 C suites and ten Python checks passed normally and with
ASan/UBSan in WSL. Clean H4 and H builds passed; H4 text/data/bss are
115,972/552/48,468 bytes, H values 98,016/328/15,104. The H4 binary is 116,524
bytes, leaving 212 bytes below the protected calibration boundary. Standard CMSIS
stack-pointer clobber warnings remain. Firmware-font previews were inspected for
the profile selector, capture prompts and mode guidance. H4 SHA-256:
`0cad21f6eb2a2b2f0b62abe3e670fa01efd9939a29dc3cef2c21d28ea2daac3a`.
The image was flashed, readback-verified and started on 205431682033. A full backup
was taken first, and storage from `0x0801C800` through the end of flash was confirmed
unchanged before restart. Physical OSL accuracy, both-profile capture/save/restart,
mode-confirmation persistence and USB/Saver regression checks remain pending.


## HF multiband overview

In ANT, open BANDS then MULTI HF. Eight mini plots cover 80/75, 40, 30,
20, 17, 15, 12 and 10 metres. Each complete 51-point band sweep replaces
only that band's cached data; the display continues to compose complete LCD
strips without exposing an intermediate VNA background. The eight bands are
measured sequentially, not simultaneously. MIN/MAX are sampled extrema across
the whole band, not interpolated resonance estimates. Curves share a 1–5 SWR
scale with dashed 2:1 and 3:1 guides; numerical extrema remain unclipped up to
99.9, then show 99.9+. Missing data displays --; invalid samples break the curve
and add ! beside the extrema. CAL/UNCAL describes each cached measurement.

PAUSE/RUN holds or resumes scanning. Tap a card to open its ordinary ANT view
at the selected single-band point count (101 by default). Roller rotation
selects a card; pressing opens it. BANDS returns to the picker. Re-entering
MULTI HF starts a fresh overview; startup now restores the saved band/MULTI view.
The overview is a fixed initial HF selection and does not alter saved favorites.

The cache shares the existing 401-float CCM allocation with the single-band
curve; it does not allocate a framebuffer or another calibration record.
Automatic range transitions run in the engine's preparation callback, after
snapshot readers release the previous sweep and before the next channel mask
is calculated. No band advances on a partial sweep or while paused. External
range changes suspend advancement until the user resumes the overview.
Protected HF ANT calibration is interpolated at each band's 51 frequencies.

Hardware checks for this addition: enter MULTI HF, let all eight plots populate,
pause/resume, open every card, rotate/press the roller, and check a fresh
battery-only startup. Verify real calibrated SWR against single-band readings;
previews use synthetic data and do not establish RF accuracy.

Multiband validation (2026-10-12): normal and ASan/UBSan host suites passed,
including extrema, cache retention, eight-band cycling, pause, invalid samples,
card/roller navigation and engine snapshot ownership. WSL H4 and H builds passed.
H4 text/data/bss: 116180/552/48468; binary 116732 bytes, erase-rounded end
0x0801C800. The code partition is almost full (4 bytes spare); further additions
require size savings. Menu/display and USB command paths use size optimization;
RF capture retains O2. Temporary USB stack/phase diagnostics were removed; reset
cause reporting and the verified IRQ scheduling fix remain.

Flashed H4 SHA-256:
`25b7e1f9f2157c63d59d7810e7e73c2f625be4c566d2ed989564157ebb1c2d81`.
CubeProgrammer verified the image on device 205431682033 and started it. All
145408 bytes at/above 0x0801C800 matched the pre-flash backup before startup.
Artifacts and previews: `../NanoVNA-X-builds/antenna-multiband/`.
Physical multiband display, battery and RF checks are awaiting user feedback.


### Direct return and CW guides (2026-10-12)

HF single-band sweep pages now move the frequency left and provide a MULTI
button beside it, returning directly to a fresh overview. TUNE and VHF/UHF do
not show this shortcut. Overview plots mark the existing Extra-class CW/data
boundary with a red vertical line; 30 m deliberately has no line.
UI tests pass normally and under ASan/UBSan, including direct return, exclusion
from TUNE/VHF, and guide positions/30 m omission. The WSL H4 build is 116576 bytes
(160 bytes below the code boundary); H4-only display size optimizations preserve
the RF optimization settings. Flashed and verified on device 205431682033;
all saved data remained identical before restart. Artifact directory:
`../NanoVNA-X-builds/antenna-multi-shortcut/`. SHA-256:
`de8ff3ad6aab14ce49722a81d406428c9595a01ecea590631243e62a9b0d78dc`.

## Cable measurement and AntExpert 0.1.1 (2026-10-13)

BANDS now opens CABLE, a 401-point S11 measurement with physical length in metres
and feet, two decimal places, adjustable velocity factor, and quarter-/half-wave
targets at a keypad-entered frequency. ANT returns to the previous band. Startup
remembers CABLE as a view, taking fresh data with the HF reference range and
14.2 MHz target; measured lengths are not stored.

HF CAL reuses the HF ANT profile over 28–29.7 MHz. FREQ CAL measures within the
band containing the target, automatically choosing HF or V/U ANT calibration.
The phase-delay estimator unwraps adjacent S11 phases and fits a straight line;
weak reflections, non-finite data, unresolved slopes, excessive curvature and
nearly aliased phase increments are rejected. Target frequencies in gaps use
the explicitly labeled HF reference measurement. Calibration interpolation stays
inside a stored band segment. Existing profiles/slots and the ANT calibration
grid are unchanged; no additional saved calibration record is needed.

Optional 50 kHz–5/50/300 MHz ranges retain the first-quarter-wave resonance
method and ordinary continuous-range calibration. ANT's segmented coefficients
are never applied across gaps. CAL opens the ANT walkthrough in HF/FREQ CAL,
or the ordinary calibration menu in a wide range. The measurement temporarily
removes electrical-delay compensation and restores it when leaving CABLE.

H4 flash savings use word-aligned functions and size optimization for formatting,
display geometry, post-sweep analysis, calibration calculation and settings
handlers. RF capture, DSP, interrupt and SPI/DMA transfer paths keep their
existing optimization. The numeric font uses a dictionary of 16-bit rows plus
repeat counts; decoding writes one glyph to caller-owned stack storage, and all
23 glyphs match the original pixels. Standard VNA features remain enabled.
`tools/pack_numfont.py` regenerates the packed font from its retained original.

Regression coverage includes synthetic cable lengths and velocity factors,
HF/V/U band boundaries, missing/incompatible calibration, keypad cancellation,
paused VF updates, formatter bounds and exact font round trips. Normal and
ASan/UBSan suites passed before the version bump, and the WSL H4 image fit the
114 KiB code partition. The cable build was flashed with a full readback and
unchanged saved data; hardware comparison against known lengths remains pending.
See the [firmware verification record](firmware_hashes.md) for hashes and the
[illustrated guide](antenna_expert.md#6-measure-a-cable-and-calculate-quarter-half-wave-lengths)
for operation and calibration reference-plane limitations.
