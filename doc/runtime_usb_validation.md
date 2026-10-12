# Runtime and USB reliability corrections

## Release notes

The higher-priority sweep task now sleeps while waiting for DMA capture instead
of using WFI, allowing the USB shell to run during continuous sweeps. DMA still
enforces RF settling/discard timing. Capture and PLL waits handle timer rollover;
ADC startup has bounded readiness/calibration waits, and board initialization
is performed once. Sweep-start settling is applied only to the first point.

USB data waits for a complete, postprocessed sweep and protects its single
buffer until the reader releases it. Incomplete sweeps and failed SD imports
are not published. Pending readers can run before the next writer starts.
The CDC DTR callback preserves active USB queue ownership and uses the ISR
lock when waking RTOS waiters. The F072 software square root avoids a signed
left shift for negative exponents.

## Validation

On 2026-10-12, USB queries failed during continuous sweeping but recovered
immediately when the user paused the sweep. With the scheduling correction,
12 alternating CH0/CH1 reads returned 401 numeric pairs each across three serial
sessions, with version and resume queries in each session. Reads took 1.56-1.63 s.
The flashed H4 image was verified by STM32CubeProgrammer; SHA-256:
`33e1251a63b0c0a92d7281dc245985dffdd0a48ebb751f72d42b89da23604c46`.

That physical NanoVNA-H4 rev4.4 / ZeeTK test used an integration image containing
the separately proposed ZeeTK RF profile. This branch alone has not been flashed
on legacy hardware. The user confirmed normal display sweeping and a successful
NanoVNA-Saver sweep on COM13 with the integration image.
Host regression and sanitizer suites pass independently on this branch. Both target
builds use WSL ARM GCC 14.2.1; F072 requires USE_LTO=yes with this toolchain.
There is no physical NanoVNA-H test or exhaustive RF accuracy/settling claim.

The ZeeTK RF profile and Antenna Mode are separate work. The snapshot API still
supports one USB reader, not multiple concurrent snapshot consumers. Slow-host,
paused/partial-sweep, time-domain, and SD error checks remain useful bench cases.
