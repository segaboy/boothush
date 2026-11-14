# PC286 scheduled keyboard accounting

2026-11-14. Question: did a bounded run actually submit all scheduled 286
keyboard bytes before its stop? Guest state alone cannot answer that question,
and acceptance by the model is still not proof of guest consumption. The
`pc_keyboard_input` result, specified in [PC/AT](pc-at.md), reports accepted,
rejected and pending counts. Scheduled bytes still fail the run on rejection;
the new field does not change model input semantics or physical behavior.

`tests/test_pc286_devices.py` checks an accepted make/prefix pair, a stop
before either byte, and model-buffer saturation with one rejected and one
still-pending byte. With the executable below, all 23 PC286 tests and four
optional port integration tests passed. The latter used the three previously
pinned Not Super Tilt Bro title and C probe ROMs; their build identities and
limitations are in `C:/Repos/Not-Super-Tilt-Bro/docs/windows-native-c-evidence.md`.
The full local output is in `C:/Repos/.task-temp/boothush-pc-input-accounting-twins-20261114-b/pc286-tests.log`
and `port-tests.log`.

The experimental Windows executable SHA-256 is
`62daf36a8d1e034667fbd73dd09a2a6134a6a0144ea56010d327b41fd110d9da`.
`scripts/build_pc_input_accounting.py` compiled the current `main.c` twice
and linked it against the existing previously tested core objects and
libraries from the base executable SHA-256
`5d4f610e98cbb8b03476f28929d6e5c0f64b941aafac3c7d5b6ef12c76c7eeb3`.
The resulting twins match; their full input hashes and commands are in
`C:/Repos/.task-temp/boothush-pc-input-accounting-twins-20261114-b/report.json`.
This deliberately reuses existing object files and is not a source-complete
or distribution-qualified build. It does not build the paused x64 work.

Reproduce with fresh output and caller-supplied local fixtures:

```powershell
$py = 'C:/Users/segab/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$base = 'C:/Repos/.task-temp/boothush-replay-series-20261114-a'
$bin = 'C:/ClasSICK/bootstrap-clean-source-verified/.tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/bin'
& $py -B scripts/build_pc_input_accounting.py --base $base --compiler-bin $bin --output C:/Repos/.task-temp/NEW-INPUT-BUILD
$env:BOOTHUSH_EXE = 'C:/Repos/.task-temp/NEW-INPUT-BUILD/a/boothush.exe'
$env:BOOTHUSH_TEST_TMP = 'C:/Repos/.task-temp'
& $py -B -m unittest discover -s tests -p 'test_pc286*.py' -v
```

For the optional port tests also set `BOOTHUSH_STB_TITLE_ROM`,
`BOOTHUSH_STB_NATIVE_C_ROM`, and `BOOTHUSH_STB_NATIVE_COMPONENT_ROM` to the
fixtures identified in the port evidence, then use discovery pattern
`test_mac_port*integration.py`.

An initial pair in the sibling `-a` directory differed in PE timestamp, build
time text and derived identifier. The successful build sets `SOURCE_DATE_EPOCH`
and suppresses the linker timestamp. No artifact was overwritten.

The updated ClasSICK 286 session used this BootHush executable to compare the
actual shared window owner and compositor against its host oracle. All eight
state/pixel checkpoints passed; at the final checkpoint all 63 scheduled bytes
were accepted with zero rejected or pending. The subsequent Mac/286 run
matched eight checkpoints and replayed the Mac trace at 4,096 modeled cycles
per edge. Canonical commands, image hashes and limits are recorded in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
These are bounded modeled inputs, not human, real-hardware, production-boot
or release-qualification results.
