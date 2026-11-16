# Stopped Macintosh CPU observations

`--mac-cpu-state 1` adds one `cpu` object to the normal Mac JSON result:

```json
{"pc":4194560,"sr":9984,"d":[0,0,0,0,0,0,0,0],"a":[0,0,0,0,0,0,0,16384]}
```

This is an illustrative layout, not a captured result. `d` lists D0-D7 and
`a` lists A0-A7 as unsigned 32-bit values; `sr` is the complete 16-bit status
register. `pc` is the stopped instruction-boundary PC. A7 is the currently
active stack, not both supervisor and user stack pointers. No prefetch,
microinstruction or exception-internal state is claimed. Invalid values and
duplicate options are refused; the option belongs only to the Mac runner.

```powershell
& $bh --machine mac128k --rom $rom --cycles 1000000 --timeout-ms 5000 --until-pc $ownBoundary --mac-cpu-state 1
```

Every command resets the guest. To compare two boundaries, repeat the same
image, executable, configuration and scheduled inputs with the two stops.
Check both actual stop results and cycles; a timeout or unreached boundary
is not a successful observation. Input schedules can be timing-sensitive.

The C API `mac128k_read_cpu_state(machine, state)` rejects null pointers and
copies the CPU's existing public register getters. It does not execute an
instruction, read a device, modify registers or alter emulated timing. Call
it only when the machine is stopped; concurrent access is not supported.
Moira remains the separately licensed CPU dependency. Only its public getter
interface was needed for this adapter; this is not an independent CPU model.

## Verification, 2026-11-16

The source change is based on `60a4cc85e06ed3baa21eb301daeb1f5fce53b2aa`.
The active-profile twin build is retained outside Git in
`C:/Repos/.task-temp/boothush-cpu-state-active-20261116-a/report.json`.
Both executable copies have SHA-256
`a4c51633e9ee1a2a935eae270f54959d2d72f5ea3f6bd8c5a085264904d0edd7`.
The report pins source inputs, compiler and exact commands. It reuses the
previously built paused x64 archive without developing or rebuilding that lane.

With `BOOTHUSH_EXE` set to that executable, from `tests`:

```powershell
python -B -m unittest test_mac_cpu_state test_mouse_witness_cli test_cpu_mask_probe test_pc286 test_pc286_devices test_pc286_pair_canvas
```

50 tests ran: 48 passed, two optional external-fixture cases skipped.
`focused-tests.log` in that build directory retains the result. Three new
test methods exercise all 16 register sentinels, PC, five SR/CCR/IPL patterns,
malformed options and PC refusal. An observed/unobserved pair compares the
entire remaining JSON and frame bytes. Fixtures are original instruction
sequences, not Apple code. Default output remains unchanged.

Downstream canonical receipts live in
`C:/Repos/ClasSICK-0.97/docs/development/windows-m68k-toolchain-20261114.md`
and `C:/Repos/Not-Super-Tilt-Bro/docs/native-call-preservation.md`.
They identify actual ROMs, native compiler contracts, registers, stack checks,
deliberately corrupted controls and limits. Those results establish their
sampled software behavior within BootHush, not physical hardware, full OS or
game compatibility, or release qualification.
