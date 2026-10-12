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
- **FAV + / FAV \*:** toggle and immediately save the current preset as a favorite.
  This uses the existing configuration save operation, which also saves other
  current device configuration settings. A failed save restores the old favorites.
- **SWEEP:** repeatedly measure the full preset at the previous VNA point count.
- **TUNE:** start at the lowest measured SWR frequency, or the band center before
  a complete sweep is available. Use the jog wheel or the large frequency buttons
  to move by 1/200 of the preset width (minimum 10 Hz), clamped to that preset.
  The instrument measures 21 points at that same frequency and displays the middle
  sample after each completed sweep. This is continuous updating, not instantaneous
  feedback; RF bandwidth and settling settings still determine update speed.
- **CAL:** return to a sweep of the whole preset and open the existing calibration
  workflow. Perform the usual open/short/load calibration on CH0 and save it there.
- **VNA:** restore the prior frequency range, point count, frequency/time-domain
  selection, reference impedance, and running/paused state. Calibration changes
  made while in Antenna Mode remain available.

SWR is referenced to 50 ohms. The graph spans SWR 1 through 5; larger values
clip at the top. The numeric readout displays 99.9+ above its readable range.
Invalid samples are omitted rather than presented as an excellent match.
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
160 m, 80/75 m, five individual 60 m channel windows, 40 m, 30 m, 20 m, 17 m,
15 m, 12 m, 10 m, 6 m, 2 m, 1.25 m, 70 cm, 33 cm, and 23 cm. Each 60 m window
is 2.8 kHz wide, centered at 5.332, 5.348, 5.3585, 5.373, or 5.405 MHz.
These dated measurement presets are not a current transmission authorization
or a license-class filter. In particular, 60 m is not treated as one continuous
band. Upper-frequency presets do not imply uniform measurement accuracy.

Favorites use the three existing reserved configuration bytes: 20 band bits and
a four-bit `0xA` format marker. No configuration size or calibration-slot layout
changes. Uninitialized/cleared configuration defaults to 40 m, 20 m, 6 m, 2 m,
and 70 cm; an explicitly empty list stays empty. Antenna Mode itself is not
automatically entered at boot. Preset order is part of the storage format.

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

Before considering this feature hardware-validated:

1. Enter ANTENNA and verify large labels and all four bottom touch targets.
2. Select several HF and V/UHF bands; check frequency limits and paging.
3. Toggle favorites, reboot, and verify persistence, including an empty list.
4. Calibrate a band with known standards. Check a 50-ohm load near 1:1,
   an open/short at high SWR, and a known mismatch against the ordinary VNA view.
5. Check a real antenna's minimum and 2:1 crossings against the ordinary SWR trace.
6. Enter TUNE; verify frequency buttons/jog direction, limits, responsiveness,
   and return to full-band calibration through CAL.
7. Check out-of-range/interpolated calibration labels and return to VNA from
   both running and paused entry states. Verify prior settings are restored.
8. Exercise USB capture/Saver alongside the display and check for flicker,
   missed touches, watchdog resets, or unexpected measurement stalls.

Multiband overview tiles and noncontiguous band-only scanning remain follow-up
work; this first version performs one uniform sweep within the selected preset.
