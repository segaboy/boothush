# Cycle-qualified instruction stops

`--until-pc ADDRESS --until-pc-after CYCLE` stops at the first instruction/CPU
step boundary whose physical PC matches and whose absolute modeled cycle is
at least CYCLE. It works on Mac128K and PC286. It does not pause at CYCLE if
the PC differs, count calls, change input timing, or modify the guest. Every
run still resets the guest; use identical images and input schedules when
comparing two boundaries. NES does not accept this new option.

CYCLE is an unsigned integer, including zero, no greater than `--cycles`.
The option requires `--until-pc` and cannot repeat, including repeated zero.
The default is zero, retaining the existing stop behavior. Normal cancellation,
fault, timeout, halt, replay and cycle limits remain. An unreached PC returns
the existing unsuccessful predicate result. At a matching cycle-limit boundary,
the existing condition-before-budget ordering still applies.

For a before/after call check, first find the desired call after a chosen
cycle. Use that observed before-call cycle as the lower bound for the return
PC run; this avoids selecting a previous iteration's return. Confirm the
actual stop, register/stack/frame state, and call order.

## Verification, 2026-11-16

See also the Mac low-RAM step predicate below for observing execution after a
disk loader transfers control.

Original Mac and 286 looping fixtures verify ignored early visits, zero/default
equivalence, inclusive thresholds and a changing guest counter. Mac also checks
full stopped-state equality against an ordinary cycle-budget control, and an
unreachable later PC. Invalid, missing, repeated and out-of-budget arguments
are rejected. No external ROM is needed.

Active-profile twins in
`C:/Repos/.task-temp/boothush-pc-after-active-20261116-a/report.json` have
SHA-256 `8860227d991a6c6d252815019c2458a1b0af3cae49306ce43e7fdaff5dd04e3b`.
The source/tool hashes and commands are retained there. As in the existing
active-profile build, the paused x64 archive is linked unchanged; this is
not a source-complete distribution build. No x64 work was resumed.

`focused-tests-final.log` in that directory records 60 tests: 58 pass and two
optional fixture cases skip. The initial run exposed a test expectation error:
an unmet PC predicate correctly exits 1 at the cycle limit; the assertion was
corrected, with no implementation change. Reproduce from `tests` with
`BOOTHUSH_EXE` set to the twin:

```powershell
python -B -m unittest test_run_controls test_mac_cpu_state test_mouse_witness_cli test_cpu_mask_probe test_pc286 test_pc286_devices test_pc286_pair_canvas
```

The port independently verifies two movement calls after both players have
jumped out of side-attack recovery, plus ten existing call sites and three
bad-callee controls. See its `docs/recovery-jump-evidence.md`. ClasSICK's fresh
286 session and Mac paired comparison are recorded in
`C:/Repos/ClasSICK-0.97/docs/development/input-followup-20261116.md`.
These are software observations, not physical timing, a complete game, or
paired-release qualification.

## Mac low-RAM step counts

`--until-mac-ram-steps START:LENGTH:COUNT` returns after the requested
number of modeled CPU steps that begin inside the half-open range
`[START, START + LENGTH)`. Use it to check a disk loader's transfer of
control when only addresses and counts should be collected. Choose the
known entry address and a length of two for a single aligned 68000 entry PC.
The regular `pc` field is the next CPU boundary after the last step.
For example, this observes one step at guest address 0x100c:

```powershell
& $exe --machine mac128k --rom $probe --floppy $disk --cycles 4000000 --timeout-ms 5000 --until-mac-ram-steps 0x100c:2:1
```

Only low RAM within 128 KiB counts, with the reset ROM overlay disabled.
Time in ROM, including the low-address ROM alias during reset, contributes
zero. Visits can recur across branches or returns. `mac_ram_steps` records
the requested address, length and target, the number of matching CPU steps
started and returned, and the first and last matching starting PC. Those PC
fields are zero until the first match. The observation reads no guest bytes.
The count is at CPU dispatch boundaries and can include exception processing
or repeated stopped-CPU steps. It cannot establish that a particular opcode
retired. A modeled bus fault or CPU halt during a matching step stops the
run before that step counts as returned. A missed target reports the normal
unsuccessful run status and partial counts. Choose this as the sole stop
predicate. Cycle and wall-clock limits still apply.

The original raw disk fixture in `tests/test_mac_floppy.py` reads an
executable sector into RAM. After a transient checksum fault, the first
entry step returns with PC eight bytes past its start. A persistent checksum
fault leaves zero matching steps. The CPU fixtures also exercise range exits,
later re-entry, a synchronous fault, and the low-address ROM overlay. Healthy
stops match the complete CPU state and modeled cycle of ordinary PC stops.

The 2026-11-19 active-profile twins under
`C:/Repos/.task-temp/boothush-ram-step-20261119-a` produced executable
SHA-256 `c709c7f10b5f8e2a908cc2f06a74192269073dda52e8aa4f7d23a80dac89ac28`.
The directory contains `report.json` with build inputs and `tests.log`
with 88 passed cases and two optional fixture skips.
