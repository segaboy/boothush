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

## Write-failure follow-up

The later `--fail-hard-disk-write` option injects an analogous failure into
selected built-in firmware writes; see [PC/AT](pc-at.md) for its scope and
snapshot semantics. A separate generated guest checked the returned carry and
status, an unchanged failing-disk snapshot, and a successful unselected
write. With executable SHA-256
`370f7a12a31bed9908813686a0cbd6bb197065b5e5d3e443fc48ad843e69f28d`,
25 PC286 tests and four optional port tests passed. The twin-build manifest,
object hashes, commands and test logs are in
`C:/Repos/.task-temp/boothush-pc-hard-fault-twins-20261114-c`.

The ClasSICK team used the injector to test a fresh-disk diagnostic copy:
failure of its write left the generated blank snapshot unchanged, while a
failure of the later verification read left the completed write in place.
The latter is explicitly not rollback. The guest also copied and read back
the full diagnostic prefix, the saved disk matched the intended image, and a
separate BootHush boot reached the diagnostic's initial state. Full evidence
and limits are in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
This remains a destructive, disposable model experiment, not an installer for
existing media or proof of physical behavior.

On 2026-11-15, the ClasSICK diagnostic copy was split into three modeled
track-bounded writes and three reads. Each was failed independently with
these injectors; full saved-disk comparisons show which earlier writes remain.
A separate test-only readback-RAM mutation exercised the mismatch stop. See
the same ClasSICK evidence for commands and results. These tests do not model
partial sectors, power interruption or physical-media recovery.

## One-shot read failure and retry

On 2026-11-15, `--fail-hard-disk-read-once` added a single modeled failure
at a selected valid starting CHS. The [PC/AT guide](pc-at.md) defines the
interface, selection, accounting and limits. A separately generated guest
verified that the first read left its RAM buffer unchanged, a disk reset and
two subsequent matching reads succeeded, and the existing persistent option
still failed on both attempts. Invalid drive selections and duplicate options
were rejected; source media remained unchanged. All 26 286 integration tests
passed with the active-profile twin executable SHA-256
`ef7b5d9cf89fd0cc0eb1a816b5e489ffc34931349370a178c0579267dcc2ff2a`.
The log and matching twin build manifests are in
`C:/Repos/.task-temp/boothush-transient-active-twins-20261115-b`.

ClasSICK's independently written disposable diagnostic now offers up to
three reads with a reset between failures. Its 44 loader cases include
transient and persistent failures at each track and an intentionally failed
reset. The actual 286 window image separately recovered from a transient
failure at each of its three payload reads, and stopped after three injected
failures in each corresponding persistent case. Commands, guest state,
image hashes and model limitations are in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
These results do not establish physical BIOS behavior or a production recovery
and installation procedure.

The later diagnostic saves its retry counter across read/reset calls. A
test-only guest overwrote that register immediately after each modeled call;
the protected version recovered or stopped at the three-attempt bound, while
the unprotected control exceeded it. The 47-case loader report and full
linked-session results are in the ClasSICK evidence above. This tests guest
robustness against a synthetic clobber; BootHush was not changed to claim a
physical BIOS clobbers the register.

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
