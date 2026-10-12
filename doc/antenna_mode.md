# H4 Antenna Mode (development branch)

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
- **FAV + / FAV \*:** toggle and immediately save the current preset as a favorite.
  This uses the existing configuration save operation, which also saves other
  current device configuration settings. A failed save restores the old favorites.
- **SWEEP:** repeatedly measure the full preset at the previous VNA point count.
- **Roller left/right:** cycle backward/forward through favorite bands in band
  order, wrapping at either end. Selecting a favorite leaves the picker or TUNE
  and starts a full-band sweep. An empty list does nothing; a single favorite
  stays selected. Pressing the roller still opens/closes the band picker.
- **TUNE:** start at the lowest measured SWR frequency, or the band center before
  a complete sweep is available. Use the large frequency touch buttons
  to move by 1/200 of the preset width (minimum 10 Hz), clamped to that preset.
  The instrument measures 21 points at that same frequency and displays the middle
  sample after each completed sweep. This is continuous updating, not instantaneous
  feedback; RF bandwidth and settling settings still determine update speed.
- **CAL:** return to a sweep of the whole preset and open the existing calibration
  workflow. Perform the usual open/short/load calibration on CH0 and save it there.
- **VNA:** restore the prior frequency range, point count, frequency/time-domain
  selection, reference impedance, and running/paused state. Calibration changes
  made while in Antenna Mode remain available.
- **Power-on mode:** entering ANTENNA saves Antenna as the startup mode; leaving
  with VNA saves VNA. Mode restoration runs after normal UI/plot initialization
  and does not write flash. Like favorites, mode changes save the whole device
  configuration. A failed save reports an error and retains the previous saved
  startup choice. Only the view is remembered; Antenna starts at 20 m after boot.

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

Favorites use the three existing reserved configuration bytes: 20 legacy band
bits and a high nibble of `0xA` for VNA startup or `0xB` for Antenna startup.
Existing `0xA` favorites remain valid and default to VNA startup. The 16 visible presets are mapped onto
those original bit positions. Any saved 60 m channel favorite selects the combined
60 m entry; saving it sets all five old channel bits. Older antenna prototypes
that only recognize `0xA` will fall back to default favorites for `0xB` records.
No configuration size or calibration-slot layout
changes. Uninitialized/cleared configuration defaults to 40 m, 20 m, 6 m, 2 m,
and 70 cm; an explicitly empty list stays empty. Cleared or unrecognized settings
start in VNA. The legacy bit mapping is part of the storage format.

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
6. Enter TUNE; verify frequency touch buttons, limits, responsiveness,
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
