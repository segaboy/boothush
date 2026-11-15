# Reproducible active-profile build

2026-11-15. The earlier integration executable rebuilt only the CLI and 286
objects against previously built core libraries. To strengthen the current
Mac/286 and port test evidence without resuming paused x64 development,
`scripts/build_active_profiles.py` configures two fresh Windows builds and
compiles the active Mac, 286, NES, CPU, device and CLI objects from this
checkout. It links the existing, hash-pinned x64 media archive unchanged,
because the current CLI references it. No paused x64 source is compiled.
This is not a source-complete distribution build or third-party rights review.

Two independent directories produced identical executable SHA-256
`678f374a8adad6a32ea31f8da69ccf15c75f6568a7d679f56bbb73952e93890b`.
The archive SHA-256 is
`1720431862157caa3a44c2cbd47fb4c6f64b75d5b3b22ccf70d0dd9b1b322ce6`.
The report records exact commands, hashes of source and tools before/after,
object/library identities and compile databases in
`C:/Repos/.task-temp/boothush-active-twins-20261115-a/report.json`.
The checkout's pre-existing modified `CMakeLists.txt` was used and hashed;
the build selected only the listed active targets and CLI objects. The hash
record is not an independent provenance review or an authenticity guarantee.
The vendored Moira and MAME CPU dependencies remain subject to their own
notices and the receiving project's policies.

Separate refusal controls preserved the existing twin report and rejected an
incorrect retained archive before creating the requested output. Logs are
`C:/Repos/.task-temp/boothush-active-existing-refusal-20261115-a.log` and
`C:/Repos/.task-temp/boothush-active-bad-archive-refusal-20261115-a.log`.

With the first twin, all 25 `test_pc286*.py` cases passed and the latest
port's two pinned title/input tests passed. Their logs are in that output
directory. The port's separate eight combat and mutation scenarios passed in
`C:/Repos/.task-temp/nstb-active-twin-combat-20261115-a/report.json`.
ClasSICK's 33 fixed-disk diagnostic cases, disposable-copy faults, eight-state
shared session, Mac/286 parity and nine snapshot controls also passed. Their
canonical commands, images and limitations are linked from
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
These are bounded emulator observations, not human or physical qualification.

Reproduce using a new output directory:

```powershell
$py = 'C:/Users/segab/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$tools = 'C:/Repos/ClasSICK-0.97/.tools'
& $py -B scripts/build_active_profiles.py --cmake "$tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe" --ninja "$tools/ninja/ninja.exe" --compiler-bin "$tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/bin" --x64-media-archive C:/Repos/.task-temp/boothush-replay-series-20261114-a/libboothush_x64_media.a --output C:/Repos/.task-temp/NEW-ACTIVE-TWINS
```

The retained archive is a build dependency, not an authorization to develop
the x64 lane. Generated executables and inputs remain outside Git.

The later one-shot disk-read injection produced another matching pair, SHA-256
`ef7b5d9cf89fd0cc0eb1a816b5e489ffc34931349370a178c0579267dcc2ff2a`.
Its full source/tool and retained-archive identities are in
`C:/Repos/.task-temp/boothush-transient-active-twins-20261115-b/report.json`.
On this pair the 26-case PC suite, two title checks, and eight game combat
scenarios passed; the linked ClasSICK evidence records the retry and paired
window checks. The same partial-build and provenance limits apply.
