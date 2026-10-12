# Easy firmware update: NanoVNA-H4

**H4 only — STM32F303, 4-inch display.** Use this fork's **H4.bin** (or its H4
DFU package), never the smaller H image or firmware for V2/SAA-2/LiteVNA.
No SD card is needed; the update travels over a USB data cable.

## 1. Get the firmware and tool

- Get an **H4** build from this repository's [Firmware CI artifacts](https://github.com/jebcampbellhm/NanoVNA-AntExpert/actions/workflows/firmware.yml)
  for `antenna_expert`, or build `H4.bin` using [the build guide](building.md).
  Artifact downloads require a GitHub login. If a release is available, use its
  explicitly labeled H4 asset. Unzip downloads before opening the binary.
- Install ST's [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html),
  including its USB DFU driver on Windows.
- Close NanoVNA-Saver and other software using the device. Keep a backup of the
  firmware/settings you want to return to before the first update.

## 2. Enter DFU with the physical button

1. Turn the H4 **off**.
2. **Press and hold the roller/multifunction switch inward** (the click action,
   not left/right rotation).
3. Keep it pressed while switching power **on**, then release it after startup.
4. Connect the H4 to the computer with a **USB data cable**.

A **black screen is expected** in DFU mode. The computer should detect
**STM32 BOOTLOADER**, normally USB VID/PID **0483:DF11**, rather than the usual
NanoVNA serial COM port. This physical-button method is documented in the
[manufacturer's H4 DFU instructions](https://nanovna.com/?page_id=103).

If you still see the ordinary sweep screen, repeat the off/hold/on sequence.
If the computer finds nothing, try another data cable/USB port and check the
STM32CubeProgrammer DFU driver. A charging-only cable cannot transfer firmware.
You do not need to open the case for the H4 button method.

## 3. Flash with STM32CubeProgrammer (Windows GUI)

1. Select **USB**, refresh the port list and select the detected DFU device
   (usually **USB1**). Click **Connect**.
2. Open **Erasing & Programming** and select **H4.bin**.
3. For a raw `.bin`, set **Start address = 0x08000000**.
4. Enable **Verify programming** and start programming. Use the normal erase of
   the programmed sectors, **not a full-chip/mass erase** if preserving saved data.
5. Wait for successful download and verification, then disconnect and power-cycle
   normally **without pressing the roller**.

On first use, confirm **HARDWARE MODE → SAVE CONFIG**, then perform the
[ANT calibration walkthrough](antenna_expert.md#2-calibrate-hf-and-vhfuhf-together).
A normal code-only update preserves the saved-data region; a different firmware's
old calibration format/settings are not automatically a valid AntExpert calibration.

## Optional: repeatable Windows PowerShell commands

Run after installing CubeProgrammer. Administrator rights are generally needed
for driver installation, not routinely for an update with the driver working.
These commands assume one DFU device; confirm its identity in the list first.

```powershell
$programmer = 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
& $programmer -l usb
# Back up all 256 KiB on the supported STM32F303 H4 before programming.
& $programmer -c port=USB1 -u 0x08000000 0x40000 .\H4-backup.bin
# Program only the firmware image and verify it.
& $programmer -c port=USB1 -w .\H4.bin 0x08000000 -v
# Run the installed application after successful verification.
& $programmer -c port=USB1 -g 0x08000000
```

Run each command separately and check success before continuing. Use a unique
backup filename for each update. The AntExpert H4 code limit is **0x0801C800**
(114 KiB); saved calibration/configuration starts there. Published/build images
must fit this boundary, including erase-page rounding. A full backup includes
those saved records. Do not flash a backup from a different unit as a calibration.

## Linux / macOS alternative

With the device in DFU mode and `dfu-util` installed:

```sh
dfu-util -l
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D H4.bin
```

This command is for the raw H4 `.bin` image. A `.dfu` file is a packaged USB
firmware image, not an SD-card update: follow the chosen DFU tool's packaged-file
workflow rather than writing that container as raw data at an address.
For Windows users compiling in WSL, flashing from Windows CubeProgrammer avoids
having to attach the USB device to WSL.
