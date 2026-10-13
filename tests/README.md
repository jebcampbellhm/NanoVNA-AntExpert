# Test Plan

The `tests/` tree hosts host-executable checks that guard the most critical helper
functions without requiring any STM32 hardware.

- `tests/unit/` contains focused suites that link against the production sources
  and validate behaviour with a regular POSIX toolchain.  Current suites cover:
  - `test_common.c`: CLI parsing helpers (`my_atof`, `parse_line`, `packbits`, …)
  - `test_vna_math.c`: LUT-driven trig/FFT helpers used by the DSP pipeline
  - `test_measurement_pipeline.c`: integration glue that proxies sweep requests
  - `test_dsp_backend.c`: scalar DSP accumulation path that runs when SIMD is disabled
  - `test_legacy_measure.c`: RF legacy analytics (quadratic solver, cursor search, regression)
  - `test_antenna_ui.py`: ANT navigation, calibration selection, cable HF/V/U
    frequency switching, missing calibration, pause, keypad cancel and rendering
  - `test_antenna_cal.py`: segmented grids, OSL coefficients, dual-profile
    capture/storage and RF compatibility of an applied calibration
  - `test_legacy_measure.c`: also checks calibrated cable phase-delay fits
    against synthetic lengths, velocity factors, noise and invalid samples;
    the H4 variant uses 401 points and retains the wide resonance checks
  - `test_numfont.py`: all 23 compressed numeric glyphs must match the
    original pixels; checks regeneration, bounds and independent buffers
  - `test_chprintf.py` / `test_keypad.py`: numeric formats and frequency entry
  - `test_event_bus.c`: synchronous/asynchronous event bus dispatch with mailbox recycling
  - `test_scheduler.c`: cooperative task scheduler slot allocation, failure paths, and stop logic
  - `test_measurement_engine.c`: RF engine state machine, event publication, and sweep orchestration
  - `test_shell_service.c`: CLI parser/buffer handling plus deferred command queue + event bus glue
  - `test_display_presenter.c`: presenter wrappers that forward drawing calls to the active API
  - `test_si5351.c`: production RF driver with a fake I2C register bank; verifies
    ZeeTK frequency generation from 600 Hz through harmonic transitions to
    2.7 GHz, codec clock/gain, output drive, and safe profile switching; runs
    for H and H4, including low-frequency cache collisions and threshold bounds
  - `test_startup_timing.py`: compiles the production timing functions with a
    simulated clock; checks RTOS blocking and capture timeout, capture and PLL
    waits across rollover, sweep-start
    delay scope, ADC startup timeout, and configuration/generator startup order
  - `test_sweep_snapshot.py`: exercises production snapshot functions with
    controlled interleavings, partial sweeps, reader handoff and timer rollover
  - `test_snp_import.py`: runs the production SD loader with injected read errors;
    incomplete files must not publish snapshots or replace sweep settings
  - `test_usb_control.py`: exercises the production CDC control hook; opening,
    closing, repeated DTR requests, and RTS changes must preserve transfer queues
    and hold the ISR lock when waking RTOS waiters
- `tests/stubs/` provides lightweight stand-ins for headers that normally come
  from ChibiOS/HAL so that host builds can compile firmware files.

## Running locally

```sh
make test
```

The `test` target builds and executes the C suites, then runs the startup/timing,
snapshot, antenna UI, marker mapping, SD-import, and USB-control harnesses using
Python 3 and the host C compiler. Antenna math and favorites also have a C suite.
Override `HOST_PYTHON` or `HOST_CC` if needed. Failures are reported with
descriptive messages.

For memory/undefined-behavior checks, use a separate output directory and make
sanitizer errors fatal (including in the Python-driven C harnesses):

```sh
make test TEST_BUILD_DIR=build/tests-sanitized \
  HOST_CC='gcc -fsanitize=address,undefined -fno-sanitize-recover=all' \
  HOST_CFLAGS='-std=c11 -Wall -Wextra -O1 -g -fno-omit-frame-pointer'
```

## Extending

1. Drop a new `tests/unit/test_*.c` file containing `main()` and register it in
   the `TEST_SUITES` variable inside the top-level `Makefile`.
2. Reuse existing stubs or add new ones (e.g. mock `ch.h`) under `tests/stubs/`.
3. Keep test comments verbose—CI logs must tell a future maintainer what a
   regression means without opening the sources.
