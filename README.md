# NanoVNA-AntExpert

[![Firmware CI](https://github.com/jebcampbellhm/NanoVNA-AntExpert/actions/workflows/firmware.yml/badge.svg?branch=antenna_expert)](https://github.com/jebcampbellhm/NanoVNA-AntExpert/actions/workflows/firmware.yml)

**NanoVNA-H4 only: support current H4 hardware and make antenna tuning a little easier.**

This antenna firmware requires a **NanoVNA-H4 (STM32F303, 4-inch screen)**.
Do not install the H4 image on a NanoVNA-H, V2/SAA-2, LiteVNA or other model.

NanoVNA-AntExpert builds on [momentics/NanoVNA-X](https://github.com/momentics/NanoVNA-X)
and its ChibiOS runtime. The `antenna_expert` branch adds an antenna-focused
workflow for the **480 × 320 NanoVNA-H4**: readable SWR screens, amateur-band
presets, guided calibration and an eight-band HF overview. The ordinary VNA
interface is preserved: the standard VNA screens and functions remain available,
including Smith charts, trace/display controls, stimulus settings, measurements,
and the ordinary VNA calibration workflow. ANT adds another way to use the H4;
it does not replace the standard VNA interface. The two ANT calibration slots
(5/6) are reserved for the walkthrough; ordinary VNA saves use slots 0–4.

**No SD card is needed** for hardware setup, antenna calibration, saved favorites,
single-band sweeps, tuning or the multiband overview. Settings and the two ANT
calibration profiles live in internal flash. An SD card remains optional for
inherited export and file features.

![Eight-band HF overview](doc/images/antenna-expert/multi.png)

*Unless explicitly labeled as USB captures, screen illustrations use the firmware's fonts and rendering code with synthetic
RF data; they are not measured antenna results. The hardware-mode illustration
uses the production help panel with a host-rendered menu outline.*

## Switch between VNA and ANT

1. From the normal VNA display, **tap the screen or press the roller** to open
   the main menu. Tap **ANTENNA** at the top.
2. From an ANT single-band or TUNE screen, tap **VNA** in the upper-right corner.
   From MULTI, tap a band card first, then **VNA** on its detail screen.
3. **Your interface choice is saved between boots:** power off in ANT and it
   starts in ANT; switch to VNA and the next boot starts in VNA. This remembers
   the interface, not the last ANT band or MULTI page: ANT starts at 20 m/101 points.

| Normal VNA: open the main menu, then ANTENNA | ANT: tap VNA at the upper right |
| --- | --- |
| ![VNA main menu with ANTENNA](doc/images/antenna-expert/vna-menu.png) | ![ANT screen with VNA return button](doc/images/antenna-expert/ant-return.png) |

The screenshots above are USB captures from the development H4, not mockups.
[View the normal VNA screen without its menu](doc/images/antenna-expert/vna-screen.png).

**CW/data cues:** on MULTI, red vertical lines mark the stored CW/data boundaries
(no line on 30 m). On single-band sweeps, the red bar **below the graph** marks
that portion of the band; the 30 m bar spans the whole band. These use the dated
Extra-class bandplan described in the [operating guide](doc/antenna_expert.md).

## What this branch adds

- **Hardware selection on an unconfirmed H4 installation:** guidance for Direct,
  Div, Multi and ZeeTK, with an explicit **SAVE CONFIG** action.
- **Two protected ANT calibration slots:** HF ANT and V/U ANT, each with **401
  calibration points distributed among its amateur bands**, rather than evenly
  across a wide span. OPEN, SHORT and LOAD can capture both profiles at each step.
- **Single-band SWR:** large minimum-SWR and frequency readouts, 2:1 and 3:1
  guides, bandwidth indication and 101/201/401 measurement-point selection.
- **TUNE:** live SWR at a selected frequency, adjusted with the roller or touch
  buttons. Favorites and the ANT/VNA startup mode are saved.
- **MULTI HF:** eight cached mini plots for 80, 40, 30, 20, 17, 15, 12 and 10 m,
  each with minimum/maximum SWR. Tap a card for detail and **MULTI** to return.
- **Bandplan cues:** combined 60 m channel view, CW/data rails in band sweeps,
  and red vertical CW/data boundaries in the overview (omitted on 30 m).
- **Runtime and hardware fixes:** ZeeTK RF settings, startup/capture timing,
  ChibiOS interrupt-exit scheduling for battery operation, roller input,
  protected flash boundaries and USB sweep-data handling.

## Start here

1. Install the image matching your MCU: **H4/F303** for the antenna screens.
   See [easy button-DFU flashing](doc/flashing_h4.md).
2. On an unconfirmed H4 install, match **HARDWARE MODE** to the board/chip
   markings, then tap **SAVE CONFIG**. ZeeTK H4 rev4.4 uses **ZEETK**.
3. Open **ANTENNA → CAL**. Capture OPEN, SHORT and a 50-ohm LOAD at the end of
   the measurement cable, then save the HF and V/U profiles.
4. Choose **BANDS**, select a band and connect your antenna. Use **TUNE** for a
   particular frequency or **BANDS → MULTI HF** for the eight-band overview.

Read the [illustrated operating guide](doc/antenna_expert.md) for hardware-mode
selection, calibration coverage, controls and storage behavior.

## Screens

| Hardware setup | Guided ANT calibration |
| --- | --- |
| ![Hardware mode guidance](doc/images/antenna-expert/hardware-mode.png) | ![HF and V/U calibration profiles](doc/images/antenna-expert/cal-profiles.png) |

| Single-band sweep and MULTI shortcut | Fixed-frequency tuning |
| --- | --- |
| ![Single-band antenna sweep](doc/images/antenna-expert/sweep.png) | ![Tune screen](doc/images/antenna-expert/tune.png) |

## Easy USB update

Power the H4 off, **hold the roller/multifunction button pressed inward**, and
power it on while holding it. Release after startup; a black screen is normal
in DFU mode. Connect a USB data cable and flash **H4.bin** with STM32CubeProgrammer.
Follow the [step-by-step button-DFU guide](doc/flashing_h4.md), including backup,
address and verification instructions. No SD card is involved.

## Hardware and validation

| Target | Scope in this fork |
| --- | --- |
| NanoVNA-H4 / STM32F303 | Antenna UI and hardware-mode guidance. Development and hands-on testing have used a ZeeTK H4 rev4.4. |
| NanoVNA-H / STM32F072 | Legacy build retained for shared-code regression checks only. **Not supported for AntExpert; no ANT screens.** |

Select the RF mode from actual component markings; there is no reliable automatic
ZeeTK revision detection. Support for presets through 23 cm does not imply equal
measurement accuracy across that whole range. The US band presets and CW/data
cues follow the supplied March 2024 bandplan; they are measurement aids, not a
current transmission-permission check.

Host tests cover the production UI, calibration grids and OSL processing, storage
protection, sweep snapshots, startup and interrupt handling. Normal and sanitizer
tests run in WSL; H4 and H firmware builds have been verified. Hardware testing
has confirmed ANT navigation, saving, multiband display, USB/Saver operation and
battery operation on the development unit. Quantitative RF accuracy and coverage
on other hardware revisions remain work in progress.

## Build from source

```sh
git clone --branch antenna_expert https://github.com/jebcampbellhm/NanoVNA-AntExpert.git
cd NanoVNA-AntExpert
make test
make TARGET=F303 -j$(nproc)
```

Use an Arm GNU toolchain and Ubuntu/WSL; development builds currently use Arm GCC
14.2.1. For the smaller H, clean between targets and use
`make TARGET=F072 USE_LTO=yes -j$(nproc)`. The H4 code partition is almost full;
the linker must continue to protect saved calibration storage. Detailed
prerequisites and USB DFU instructions are in [the build guide](doc/building.md).
Firmware is flashed over USB DFU, not by putting a `.dfu` file on an SD card.

## Firmware checksums

CI downloads now include `H4-SHA256SUMS.txt` and `H4-build-info.txt`, recording the
binary/package hashes, source commit, compiler and target. Compare `.bin` with
`.bin`; a `.dfu` container or artifact ZIP has a different hash.

The CI firmware and the development unit's last verified flashed image are
**not byte-identical**: CI used Arm GCC 13.2.1, the local build used 14.2.1, and
firmware embeds the build date/time. A later local rebuild differed from the
flashed image in only four timestamp bytes. See the
[dated hash comparison](doc/firmware_hashes.md) for the exact artifacts and limits
of this check. Matching source does not imply matching binary hashes.

## Project notes and credits

- [Illustrated antenna operating guide](doc/antenna_expert.md)
- [Implementation notes and development validation history](doc/antenna_mode.md)
- [Easy H4 button-DFU flashing](doc/flashing_h4.md)
- [Build guide](doc/building.md)
- [Archived upstream overview](README_UPSTREAM.md)
- [Licensing and provenance](doc/licensing.md)

This fork is maintained by [@jebcampbellhm](https://github.com/jebcampbellhm).
Thanks to [@momentics](https://github.com/momentics) for NanoVNA-X,
[@hugen79](https://github.com/hugen79/NanoVNA-H) for the NanoVNA-H reference work,
and [@edy555](https://github.com/edy555), [@DiSlord](https://github.com/DiSlord)
and the wider NanoVNA community. Existing copyright notices and GPL licensing
are retained; see [LICENSE](LICENSE).
