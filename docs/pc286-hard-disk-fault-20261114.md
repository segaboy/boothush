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

On 2026-11-15 BootHush added an opt-in selected-occurrence read failure so
the same guest can first read a disk and later fail its verification read at
the same CHS. A separate original guest verified success, a no-transfer second
failure and subsequent recovery, plus not-reached, wrong-CHS and invalid or
duplicate-option cases. All 27 PC286 tests passed with active-profile twin
executable SHA-256
`3aec7ffc0062d950f8f62088baf5c17ed95c70a9f1737776157a5a38568aa7f8`.
Build sources, tools and hashes are in
`C:/Repos/.task-temp/boothush-read-ordinal-active-20261115-a/report.json`.
A follow-up test also checks that an invalid buffer crossing a 64 KiB
boundary is refused before the ordinal matcher counts a read; the next
valid matching read still triggers occurrence one. The 27-test suite passed
again on the same executable.

ClasSICK used the feature to test each post-write verification read in its
generated-disk prefix-preflight experiment. Its test and precise limitations
are in `C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
The port's native-jab title test also passed on this emulator, with its own
inputs and results in `C:/Repos/.task-temp/nstb-ordinal-regression-20261115-a/report.json`.
These are model observations, not physical media or game validation.

The opt-in `--fail-hard-disk-write-at C:H:S:N` now fails precisely one
valid matching write. An original device test writes one value, injects a
no-transfer second write, reads back the first value, writes a third value,
and verifies it both in RAM and the output snapshot. It also covers an
unreached occurrence, a different CHS, an invalid crossing buffer before a
valid match, malformed and duplicate options, and preservation of the input
image. The full 28-test PC suite passed; its log and reproducible active
Mac/286/NES build inputs are in
`C:/Repos/.task-temp/boothush-write-ordinal-active-20261115-a`. The twin
executable SHA-256 is
`3faa76ecc77c2e8d5010e1267ee76755e577ac1252a7127619d93683c432b924`.
ClasSICK used this synthetic fault to test bounded writes to disposable
images; its exact commands and scope are in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
The Not Super Tilt Bro mouse-title and side-tilt test also passed on that
build; see `C:/Repos/Not-Super-Tilt-Bro/docs/side-tilt-evidence.md`.
These observations do not prove real-media write atomicity or hardware input.

The subsequent `--fail-hard-disk-reset-at N` addition injects one failure in
the built-in fixed-disk reset service. A separate guest checked a successful
first reset, an invalid drive that did not count, the selected failed reset,
its status query and a subsequent successful reset. A valid floppy reset also
does not consume the fixed-disk occurrence. An unreached occurrence,
malformed/duplicate options and preservation of the input disk were also
checked. All 29 PC tests passed on active-profile twin SHA-256
`f795a383e4f949a62f258559041cf3dc5360b0cca29a2f146cb2507fd496b8cc`;
build inputs and test log are in
`C:/Repos/.task-temp/boothush-reset-ordinal-active-20261115-a`. The
ClasSICK test combines a selected failed write and failed reset at each of
three tracks; it stops before retrying and verifies the exact partial disk.
That report is `C:/Repos/.task-temp/pc286-reset-retry-20261115-b/report.json`.
The port title and side-tilt regression also passed on this build in
`C:/Repos/.task-temp/nstb-reset-regression-20261115-a/report.json`. This
injection does not establish physical BIOS or controller behavior.

ClasSICK also used bounded physical-memory inspection with an instrumented
diagnostic loader to observe its 286 guest's stack watermark at eight window
checkpoints. The linked evidence above records the separate known-depth
control, input identities and limitations. This exercises BootHush's memory
inspection and PC model; it is not a physical stack measurement or evidence
of a maximum for all paths.

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
