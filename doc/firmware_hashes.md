# Firmware comparison — 2026-10-12

The initial `antenna_expert` [CI run](https://github.com/jebcampbellhm/NanoVNA-AntExpert/actions/runs/38311608915)
for commit `78170831143fc5da5348d5c6a66b75ccfff19cdf` succeeded for both targets.
The Node.js 20/24 annotation concerned the JavaScript runtime of old Actions,
not an Arm firmware build failure. Workflows now select Node 24 Actions directly.

## H4 raw binary SHA-256

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Last image flashed and readback-verified on the development H4 | 116576 | `de8ff3ad6aab14ce49722a81d406428c9595a01ecea590631243e62a9b0d78dc` |
| Later WSL publication-validation rebuild | 116576 | `ac7828e5697b46212017fdf095e13b63ffffae77103b69b788a97fcf150fa1b8` |
| Initial GitHub CI artifact, F303 / H4.bin | 116124 | `8cc44cf5d8a3b8dca845ea035b422b9e4403b2626e4ca53ff5f9dd7eebbae495` |

**They do not match byte for byte.** The two WSL images differ at exactly four
bytes in the embedded build-time text (`16:14:33` versus `16:33:47`, Oct 12 2026).
All their other bytes match. CI used Ubuntu's Arm GCC **13.2.1**, while WSL used
**14.2.1**, and its embedded build time is `21:36:23`. Compiler/library and build
metadata differences prevent claiming binary equivalence with the on-device image.

The on-device reference is the saved image from the last successful programmer
verification, not a fresh DFU dump in this audit. No firmware was flashed during
this CI/documentation update. USB screen captures can confirm the running UI but
are not a cryptographic flash readback.

The CI H4 `.dfu` SHA-256 is
`7339cbb7f8ae0a8f74369d3c5cb3318b535bc35730d6b988fa6392e9a71540dc`.
It is a container around the firmware and must not be compared directly with
the raw binary hash. Artifact ZIP checksums likewise describe the ZIP, not H4.bin.

## Verify a download

New CI artifacts include `H4-SHA256SUMS.txt` and `H4-build-info.txt`. Extract them
with the firmware. In Linux/WSL, run `sha256sum -c H4-SHA256SUMS.txt` from that
directory. In PowerShell, use `Get-FileHash .\H4.bin -Algorithm SHA256` and compare
the printed value with the H4.bin line. Use the manifest from the same CI run.

For reproducible local/CI comparisons, both must use identical source, compiler,
libraries, options and build timestamp. This project still embeds `__DATE__` and
`__TIME__`; those inputs have not been made reproducible by this documentation fix.
