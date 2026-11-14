# Modeled 286 hard-disk read failures

2026-11-14. The ClasSICK hard-disk diagnostic needs to demonstrate refusal
when a valid read fails. An invalid CHS request alone does not exercise that
case. `--fail-hard-disk-read` adds an opt-in deterministic failure to the
existing built-in firmware shim; the exact interface and restrictions are in
[PC/AT](pc-at.md). It does not claim to reproduce a physical controller fault.

BootHush tests use an original generated disk and guest. They exercise first
and second read failures, carry and `AH=20h`, unchanged failing buffers,
retained earlier bytes, an unselected CHS, cylinder 614, geometry rejection,
and buffer-error precedence. The existing floppy-injection and other 286
regressions also ran. On the build below, 24 286 tests and four optional Not
Super Tilt Bro integration tests passed. Their full output is in
`C:/Repos/.task-temp/boothush-pc-hard-fault-twins-20261114-b/pc286-tests.log`
and `port-tests.log`.

Two fresh builds of `main.c` and `pc286.cpp` linked with the previously
recorded core objects produced identical executable SHA-256
`703da7b4b3883f899cff6b17d221a3afce69bcf4d04e21d07718252ed2b6c0b0`.
All linked object hashes and exact commands are in
`C:/Repos/.task-temp/boothush-pc-hard-fault-twins-20261114-b/report.json`.
The build reused previously compiled core libraries, so it is not a
source-complete distribution build. It did not build the paused x64 work.

ClasSICK's generated maximum-size diagnostic was tested with a failure at
each of four reads; the actual shared-window disk was tested with a failure at
each of its three reads. Both reached the expected diagnostic failure stop
with prior bytes preserved and failing bytes untouched. Without injection,
the shared-window disk still matched the host and Mac at eight modeled
checkpoints. Exact media, commands and observations are in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
The result does not prove a physical BIOS, install path, human input or
release qualification.

Reproduce the partial twin build with a new directory:

```powershell
$py = 'C:/Users/segab/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$base = 'C:/Repos/.task-temp/boothush-replay-series-20261114-a'
$bin = 'C:/ClasSICK/bootstrap-clean-source-verified/.tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/bin'
& $py -B scripts/build_pc_fault_injection.py --base $base --compiler-bin $bin --output C:/Repos/.task-temp/NEW-HARD-FAULT-BUILD
$env:BOOTHUSH_EXE = 'C:/Repos/.task-temp/NEW-HARD-FAULT-BUILD/a/boothush.exe'
$env:BOOTHUSH_TEST_TMP = 'C:/Repos/.task-temp'
& $py -B -m unittest discover -s tests -p 'test_pc286*.py' -v
```
