# Explicit native run budgets through 20 billion cycles

2026-11-18. Native `mac128k` and `pc286` now accept `--cycles` from 1 through
20,000,000,000, inclusive. The required `--timeout-ms` range remains
1 through 3,600,000. Both bounds are explicit; there is no unbounded default.
This supports follow-on ClasSICK workloads after its selected-media Mac
full-read probe consumed about 2.55 billion cycles. No selected media was
opened for this BootHush qualification.

## Narrow change

`src/main.c` already used 64-bit machine totals, run-loop deadlines, result
JSON and safe bounded per-step slices. The native wrappers also retain
64-bit accumulated clocks; the per-slice APIs accept small 32-bit amounts.
The CLI request budget was the narrower field. It now uses `uint64_t` and
the existing checked decimal/hexadecimal parser, with a separate 20-billion
admission constant. Zero, overflow, malformed and excessive values are
refused. The help text reflects the new limit.

Default PC and mouse witness ends previously copied that request into
32-bit fields. Those fields and the three mouse witness header formats now
preserve the full 64-bit budget. Their existing native APIs already accept
64-bit ends. No CPU or device implementation changed.

The existing 4-billion constant still bounds explicit witness ranges and
capture cycles. Scheduled input offsets and `--until-pc-after` remain
unsigned 32-bit values. Default keyboard/display witness ends remain
4 billion. NES retains its separate instruction-step interface; paused
x64 admission is unchanged. These limits have not been widened by implication.

## Exact build and evidence

Base HEAD: `5d613614dd4f17d605de75ecc3f38c9129079488`, plus this working change.
The pre-existing modified `CMakeLists.txt` was used and hashed, not edited;
pre-existing x64 guest files were left untouched.

| Artifact | SHA-256 |
| --- | --- |
| Active-profile executable, both twins | `11f594b4f1ef0a1ef805c776364710713e60fa17440b16273096b6a194cb57e5` |
| `src/main.c` | `a9b9b85869ec6245a1c9b20d7b94fee572c4c588d142486321c6276c84cfc29c` |
| `tests/test_long_cycles.py` | `664a124f418e169a56b0621c2c2bddf741cc1b2bae5786ce1c8e590a097e5146` |
| Retained x64 media archive | `1720431862157caa3a44c2cbd47fb4c6f64b75d5b3b22ccf70d0dd9b1b322ce6` |

New executable:
`C:/Repos/.task-temp/boothush-long-cycles-20261118-a/a/boothush-active.exe`.
The matching `b` twin is in the same parent directory.
`report.json` records exact build commands, all selected source/tool input
hashes before and after building, and object/library identities.
`qualification.json` records test and Python identities, complete numeric
long-run results, log hashes, and a final matching-input check.
`qualification.stdout` and `qualification.stderr` retain the final test run.

The [active-profile build limitation](active-profile-build-20261115.md)
still applies: active Mac/286/NES sources are rebuilt and the pinned x64
media archive is linked unchanged. This is not a source-complete distribution
or third-party rights review. The existing help-string overlength warning
remains (4506 bytes here, 4505 in the preceding build); no new warning class
was introduced by this change.

## Qualification

The final combined suite collected 81 tests in 21.561 seconds: 77 passed,
4 optional ClasSICK-ROM cases skipped because their fixtures were not
supplied. It includes 9 new focused cases plus existing boot, run-control,
development-control, 286, mouse-witness and CPU-state regressions.

Focused checks cover admission at 1, 4 billion, 4 billion plus one,
`UINT32_MAX`, `UINT32_MAX+1`, and 20 billion in decimal and hex; invalid and
overflowed inputs; unchanged timeout admission and actual timeout;
all three default mouse-witness headers; narrower unrelated cycle fields;
NES/x64 refusal; and 286 acceptance of the maximum budget with an original
halted ROM. The two long runs are opt-in with `BOOTHUSH_LONG_CYCLE_TESTS=1`.

| Original workload | Requested cycles | Observed cycles | Final wall seconds | Observations |
| --- | ---: | ---: | ---: | --- |
| Mac DIVU loop | 20,000,000,000 | 20,000,000,110 | 5.112 | `cycle_limit`; RAM signature `1234abcd`; D0 `0x1234`; VBL 153562 |
| 286 DIV/branch loop | 4,294,968,296 | 4,294,968,305 | 7.605 | `cycle_limit`; CS `0xf000`; IP `0x000a`; not halted |

The Mac VBL count equals observed cycles divided by 130240, rounded down.
Whole-instruction overshoot remains expected. Both runs requested a
60,000-ms guest timeout and had an additional subprocess timeout. The
earlier trial produced the identical guest results in 6.081 and 9.068 seconds.
The original generated ROM hashes are
`a4b61a82f828ab8034e643faa036ae9f3062a46a4ecb200f756ddcf1e71e5283`
(Mac) and
`c1f85ce8ee76d7e67a331a1057c0b80dd6f3f71a447e0b2a0e2a7c1405e6deae`
(286). Their generators are in the pinned test source; generated fixtures
are temporary and excluded from Git.

As a regression control, the same admission test against the preceding
`7adb8a74cef009b7c532df24458c3a5db6b77725356fdb31e5ac4966d7ed971a`
executable correctly failed all five larger-budget subcases with the old
4-billion error. `old-limit-control.*` retains this expected failure.
The first new-suite trial had one assertion mismatch: an empty argument
was correctly rejected by the generic nonempty-value check rather than
the cycle-range check. The assertion was corrected, with `long-tests.*`
retained and the full final suite passed. No guest defect was masked.

## Reproduction

From `C:/Repos/boothush`, use a new external output directory:

```powershell
$py = 'C:/Repos/ClasSICK-0.97/.tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/python/bin/python.exe'
$tools = 'C:/ClasSICK/bootstrap-clean-source-verified/.tools'
& $py -B scripts/build_active_profiles.py --cmake "$tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe" --ninja "$tools/ninja/ninja.exe" --compiler-bin "$tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/bin" --x64-media-archive C:/Repos/.task-temp/boothush-replay-series-20261114-a/libboothush_x64_media.a --output C:/Repos/.task-temp/NEW-LONG-CYCLE-BUILD
```

For the recorded build, from `C:/Repos/boothush/tests`:

```powershell
$env:BOOTHUSH_EXE = 'C:/Repos/.task-temp/boothush-long-cycles-20261118-a/a/boothush-active.exe'
$env:BOOTHUSH_TEST_TMP = 'C:/Repos/.task-temp/boothush-long-cycles-20261118-a'
$env:BOOTHUSH_LONG_CYCLE_TESTS = '1'
$env:BOOTHUSH_CLASSICK_ROM = $null
$env:BOOTHUSH_CLASSICK_WINDOW_ROM = $null
$env:BOOTHUSH_CLASSICK_LIVE_ROM = $null
& $py -B -m unittest -v test_long_cycles test_boot test_run_controls test_development_controls test_pc286 test_mouse_witness_cli test_mac_cpu_state
```

This establishes bounded CLI admission and actual original execution beyond
the 32-bit clock. It does not establish long-running device workloads,
System Disk startup, resource execution, human testing, or physical hardware
behavior. Regression fixtures use original generated media and hidden input
where needed; no owner media or visible window was used. Earlier integration
receipts keep their original binaries. Any new longer integration must pin
and qualify this executable explicitly.
