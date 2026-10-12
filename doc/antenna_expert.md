# Using NanoVNA-AntExpert

**H4 only:** the ANT workflow requires NanoVNA-H4 / STM32F303 with its 480 × 320 screen. **No SD card is
required.** Calibration profiles, hardware selection, favorites and ANT/VNA
startup mode use internal flash. Other NanoVNA models are not supported by the AntExpert UI.
Start with the [easy button-DFU flashing guide](flashing_h4.md).

These illustrations are generated with the firmware font/rendering code and
synthetic measurements. They show controls and layout, not expected antenna SWR.
The hardware help panel is production-rendered; its menu outline is host-rendered.

## VNA and ANT are two views of the same instrument

The standard VNA screens and functions remain available, including Smith-chart
and trace views, stimulus controls, measurement tools and normal VNA calibration.
ANT is an additional interface. Slots 5/6 are protected for ANT; ordinary saves
continue to use slots 0–4.

![VNA main menu: ANTENNA is the top item](images/antenna-expert/vna-menu.png)

Tap the VNA screen or press the roller, then choose **ANTENNA**. To return, tap
**VNA** at the upper right of a single-band or TUNE screen. In MULTI, tap a band
card to open that screen first.

![ANT single-band screen: VNA returns to the standard interface](images/antenna-expert/ant-return.png)

These two images are actual USB screen captures from the development H4. ANT/VNA
selection is saved automatically across power cycles. ANT restarts on 20 m at
101 points; the last band, TUNE/MULTI page and overview contents are not restored.

## 1. Select hardware on first use

![Hardware selection](images/antenna-expert/hardware-mode.png)

If the saved hardware-confirmation flag is missing, H4 opens this guidance screen
at startup. This includes a fresh installation, cleared settings or older
settings without that flag. Firmware does not infer the RF revision from USB or
MCU identification. Match the board/component markings:

| Selection | Hardware cue |
| --- | --- |
| DIRECT | Si5351A clock hardware |
| DIV | MS5351M clock hardware |
| MULTI | SWC5351 clock hardware |
| ZEETK | ZeeTK H4 rev4.4, NE602A mixers and SJWCH5351 clock |

Choose the matching option and tap **SAVE CONFIG**. Later, reopen it through
**SYSTEM → DEVICE → MORE → MODE**. Changing RF mode clears confirmation and
active calibration; save the selection and recalibrate. The hardware **MULTI**
option is unrelated to the **MULTI HF** antenna overview.

Missing confirmation does not erase existing settings or calibration slots.
Correction stays disabled until the hardware choice is confirmed. Retained old
calibration is not automatically proof that it matches the selected hardware.
For ZeeTK, use **CAL POWER = AUTO** when calibrating.

## 2. Calibrate HF and VHF/UHF together

Open **ANTENNA** from the normal main menu, then **CAL**.

![Calibration profile selection](images/antenna-expert/cal-profiles.png)

Two protected internal-flash slots are reserved for ANT calibration. Each profile
contains **401 total calibration points**, concentrated in the bands below.
It is not 401 points per band and not one uniformly spaced sweep through the gaps.

| Profile | Internal slot | Covered presets |
| --- | --- | --- |
| HF ANT | 5 | 160, 80/75, 60, 40, 30, 20, 17, 15, 12 and 10 m |
| V/U ANT | 6 | 6 m, 2 m, 1.25 m, 70 cm, 33 cm and 23 cm |

The displayed slot numbers are the firmware's IDs; normal calibration slots
0–4 remain available. Ordinary calibration saves and USB save commands cannot
write slots 5/6. The ANT walkthrough is their write path.

1. Connect the measurement cable to **CH0**. Attach standards at the cable end
   where the antenna will connect; keep adapters/cable consistent afterward.
2. Set each profile to **CAPTURE** or **KEEP**. Missing or incompatible profiles
   are automatically included and cannot be skipped. With both selected, every
   standard is measured over **both** profiles before the next prompt.
3. Tap **START**, connect **OPEN**, then **CAPTURE**. Wait for the SHORT prompt.
4. Connect **SHORT**, tap **CAPTURE**, and wait for the LOAD prompt.
5. Connect the **50-ohm LOAD**, then tap **CAPTURE**.
6. At **Ready to save**, tap **SAVE**. This replaces the selected ANT profiles.

| Capture OPEN | Save both profiles |
| --- | --- |
| ![Open standard](images/antenna-expert/cal-open.png) | ![Save calibration](images/antenna-expert/cal-save.png) |

OPEN/SHORT/LOAD supplies the one-port S11 correction used for antenna SWR.
ISOLATION and THRU are not required for ANT; use the ordinary VNA calibration
workflow for two-port transmission measurements. ANT does not need enhanced
response calibration for this S11 workflow.

Selecting a band automatically loads its corresponding profile and interpolates
within that band's stored calibration segment. The 101/201/401 measurement-point
choice and the 51-point multiband scans reuse these profiles. Gaps between bands
are not calibrated by interpolation across their boundaries; the combined 60 m
segment includes the gaps between its channels. Watch the calibration-status
message rather than assuming any arbitrary VNA range is covered.

Cancel before SAVE leaves stored profiles unchanged. Saving the two profiles is
not an atomic pair: interrupted power during SAVE may leave only one updated.
Retry a reported save failure. **Clear Config preserves ANT calibration slots**;
it does not count as recalibration. ANT profiles include RF compatibility
metadata, and incompatible profiles must be captured again.

## 3. Sweep one band

![Single-band sweep](images/antenna-expert/sweep.png)

![Band picker with favorites shown](images/antenna-expert/bands.png)

**BANDS** offers HF+6, V/UHF and FAV lists. Choose a wavelength/frequency tile;
**PTS** cycles 101, 201 and 401 measurement points. The power-on default is 101.
The combined 60 m tile shows the five channel centers on one plot.

- **MIN SWR** is the lowest sampled SWR and its frequency, not necessarily the
  zero-reactance resonance. Narrow dips between samples can be missed.
- The plot has **2:1 and 3:1** guides and a 1–5 SWR scale. Higher values clip at
  the graph ceiling; the numeric value remains visible up to `99.9+`.
- **2:1 width** describes the dip around the minimum. `>=` / `(edge)` means the
  band edge truncates that width.
- A **red rail** beneath HF plots marks the stored Extra-class CW/RTTY/data
  region. It covers all of 30 m.
- **SWEEP** switches between continuous measurement and pause, retaining the
  trace. The button changes appearance and says RUNNING or PAUSED.
- **FAV / ALL** toggles roller navigation through favorites or all bands.
  The separate **+ / \*** button adds/removes and immediately saves membership.
- Roller left/right changes bands; pressing opens/closes the band picker.
- **MULTI**, beside the frequency on an HF band sweep, returns directly to the
  eight-band overview. It is not shown in TUNE or on VHF/UHF band sweeps.
- **VNA** returns to the previous ordinary VNA view. Entering/leaving ANT saves
  which interface should open on the next boot; the chosen band and overview
  are not persisted. ANT starts at 20 m with 101 measurement points.

## 4. Tune at one frequency

![Tune controls](images/antenna-expert/tune.png)

Tap **TUNE** to start at the sampled minimum, or the band center if no completed
sweep is available. Turn the roller or tap **− FREQ / + FREQ** to change the
frequency by the displayed step. In TUNE the roller changes frequency, not band.
The live readout uses the middle sample from a 21-point fixed-frequency capture;
update speed still depends on RF bandwidth and settling. **SWEEP** returns to
whole-band measurement.

## 5. Compare eight HF bands

![HF multiband overview](images/antenna-expert/multi.png)

Enter with **BANDS → MULTI HF** or the **MULTI** shortcut in an HF band sweep.
The initial fixed selection is **80, 40, 30, 20, 17, 15, 12 and 10 m**. All eight
are measured even if they are not saved favorites; a multiband antenna may have
poor matches on some of them.

Each band gets **51 measurement points**, scanned sequentially. Completed plots
stay visible while the next band is measured; values are not simultaneous.
Each card shows its range in MHz, sampled **MIN/MAX SWR**, and CAL/UNCAL status.
Curves share a 1–5 scale with dashed 2:1 / 3:1 lines. A red vertical line marks
the same CW/data boundary used in single-band view; **30 m has no vertical line**.
`!` indicates invalid samples; the curve breaks at them instead of joining them.

- **PAUSE / RUN** stops or resumes scanning.
- **Tap a card** to open its detailed band sweep at the session's chosen point
  count; **MULTI** on that screen returns to a fresh overview.
- **Roller left/right** selects a card; **press** opens it.
- **BANDS** returns to band selection.

The overview reuses the HF ANT calibration and internal working memory. No SD
card, extra calibration file or per-band manual recall is needed.

## Reference and limitations

Presets and mode boundaries are based on the supplied March 2024 US bandplan.
They do not replace current license/band regulations or establish antenna or
instrument accuracy, particularly at the highest frequency presets. Calibration
is specific to the cable, adapters, standards and RF configuration in use.

[Build/USB DFU instructions](building.md) ·
[Implementation and validation history](antenna_mode.md) ·
[Project README](../README.md)
