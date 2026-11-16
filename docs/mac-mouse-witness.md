# Correlated Mac mouse witness

`--mouse-witness-detail 1`, together with `--mouse-witness PATH`, opts into the `BHMACMOUSE2` format. Without this option the original `BHMACMOUSE1` format is unchanged. The witness is disabled by default. It is an observation of BootHush's model, not a physical timing measurement or guest-owned omission counter. It does not write to guest memory or control the Windows mouse or keyboard. The native invisible-window fixture sends window messages; it is not physical input.

The Not Super Tilt Bro project used this witness to count phase events between
successive SCC-A reads in its title ROM. At 256 modeled cycles per edge, each
of 20 observed read intervals crossed two to four phase transitions; at 1,024
cycles, each of 85 crossed at most one. Traced and untraced runs matched their
corresponding guest state and frames. The exact ROM, executable, commands and
limits are in `C:/Repos/Not-Super-Tilt-Bro/docs/mouse-title-evidence.md`.
These model observations do not establish physical timing or isolate one cause.

## Reproduce the ClasSICK timing comparison

These commands use the original 65,536-byte candidate ROM with SHA-256 `faf37c6f96f8eb2ee6cf7a1e58a8b1b9538055d2fc89493c7419eeea816f6c8c`. `--mouse-witness` and `--frame` require new paths. Each invocation is a cold boot, executes without `--live`, and schedules a rightward movement. A caller launching a child with redirected output can specify `CREATE_NO_WINDOW` to hide its console, as in the integration guide.

```powershell
$exe = 'C:\Repos\boothush\out\windows\boothush.exe'
$rom = 'C:\ClasSICK\mac-candidate-anchored-20261112-a\classick-live.rom'
foreach ($edge in 2048, 4096) {
  $witness = Join-Path $env:TEMP ("mac-$edge-" + [guid]::NewGuid().ToString('N') + '.txt')
  & $exe --machine mac128k --rom $rom --cycles 65000000 --timeout-ms 20000 `
    --mouse-dcd-invert 3 --mouse-motion 60000000:32:0 `
    --mouse-edge-cycles $edge --mouse-vbl-guard 4096 --inspect 0xb00:1024 `
    --mouse-witness $witness --mouse-witness-detail 1 --mouse-witness-memory 0xc1c:4 `
    --mouse-witness-range 60000000:65000000
  Write-Output "Witness: $witness"
}
```

For this exact ROM, the big-endian 32-bit guest-owned pointer coordinates are at `0xc5c` and `0xc60`, and the guest omission counter is at `0xc1c` within the inspected range. These addresses are not general Mac interfaces. Replace the motion argument with `60000000:-32:0`, `60000000:0:32`, or `60000000:0:-32` to trace the other directions. The optional `BOOTHUSH_CLASSICK_LIVE_ROM` test covers all four at both intervals.

In the reproduced +X case, the 2,048-cycle trace shows the X phase at 2 when SCC interrupt level 2 became pending at cycle 60,006,236; the X phase changed to 0 at 60,008,286 while SCC pending remained set; the CPU accepted level 2 at 60,008,554. The 4,096-cycle case reached the expected final position and the 2,048-cycle case ended one pixel left with 29 guest-reported omitted passes. Both delivered 64 modeled transitions and ended with zero pending. In separate paired runs, enabling the RAM witness left the stopped guest JSON and full framebuffer identical. The 2,048-cycle trace retained 1,263 rows, including 74 overlapping RAM writes, and the 4,096-cycle trace retained 1,316 rows, including 72 overlapping RAM writes; neither dropped rows in the 60,000,000–65,000,000 range. The first observed low-word write of 29 to `0xc1e` occurred at cycle 60,147,638 in that fast case; the slow case's observed low-word writes were zero. These are separate 16-bit bus writes to a big-endian longword, not an atomic trace sample of the full value.

For the source revision previously associated with the supplied ROM (`0f8d85d`), `platform/mac128k/window-entry.c` copies `motion.motion_drops` into `cs_mac_motion_drop_trace[1]` in its foreground observer; `platform/mac128k/motion.c` increments the internal counter in `drop()`. Thus the filtered `0xc1c` writes are **published copies**, not direct observations of the individual omission decisions. The two cases correlate those copies with device events but do not establish which implementation is responsible. A next discriminating test is to identify the internal counter's address for this exact ROM from its own build map, then trace its writes alongside SCC and VIA events; a separately validated timing model would still be needed to assess hardware fidelity.

## Format and limits

The first line is:

```text
BHMACMOUSE2 FIRST LAST STOP COUNT DROPPED NO_DROPS RANGE_REACHED EDGE GUARD INVERT
```

The inclusive cycle filter is absolute even with `--input-after`; calculate a range from a previous readiness observation when needed. `STOP` is the actual stop cycle, `COUNT` the number retained, and `DROPPED` the number discarded after the fixed 65,536-row cap within the requested range. `NO_DROPS` is 1 exactly when `DROPPED` is zero. `RANGE_REACHED` is 1 when `STOP >= LAST`; it is 0 if execution ended before the requested range finished. Both are needed to assess coverage. The stop condition is checked before injecting events at that boundary, so reaching `LAST` does not prove scheduled events at that exact cycle were delivered. Events outside the selected range are intentionally filtered, not counted as dropped. The result JSON and `mouse.pending` remain separate; an early stop can leave queued work even when the witness did not overflow. When no mouse bridge is used, timing header fields are zero.

The next line names every whitespace-delimited column. All numbers are decimal. `seq` starts at zero and increases in capture order. Rows are emitted in emulator observation order; this is the tie-breaker when cycles match. `pc` is Moira's `PC0` instruction context, including at interrupt acceptance; it is not a separately timed hardware bus address. Moira's precise-timing mode is disabled: several accesses can share an instruction cycle, and CPU boundaries and callbacks do not establish real hardware bus timing.

```text
seq cycle kind pc address reg value x_phase y_phase pressed scc_pending scc_latched scc_dcd via_ifr via_ier irq_level source dx dy pending due deadline
1 60000080 input_due 4219210 0 0 2 2 2 0 0 0 0 0 2 0 1 32 0 0 60000000 0
2 60000080 sample_accepted 4219210 0 0 0 2 2 0 0 0 0 0 2 0 1 32 0 64 60000080 0
```

The example shows a scheduled request due at cycle 60,000,000 accepted at the next observed boundary, with 64 pending transitions. All device snapshots include current X/Y two-bit phases, button, SCC pending/latched/DCD states, VIA IFR/IER and IRQ level. Phase bit 0 is the live DCD input; `scc_dcd` is the effective SCC status and may be latched. The `pending` field is BootHush's outstanding bridge transitions at the event, not a ClasSICK loss or omission count.

Kinds include `snapshot`, `input_due`, `sample_accepted`, `sample_rejected`, `guard`, `phase`, `button`, `scc_read`, `scc_write`, `via_read`, `via_write`, `irq` and `irq_accept`. `input_due` appears when a scheduled mouse event is applied, not for future events that never become due. Its `due` is the absolute scheduled cycle, `value` is input kind (0 button, 1 raw phase, 2 motion, 3 injected window), `dx`/`dy` contain that request's first and second arguments, and `deadline` contains its window action if relevant. `sample_accepted`/`sample_rejected` contain the relative sample and `value` 0 unchanged, 1 release, or 2 press; `due` is its sample timestamp. `source` is 1 scheduled direct, 2 injected window message, 3 replay, 4 live-window event, or 0 not applicable. Source 4 identifies the live-window path only; it does not distinguish a person from OS automation. A no-op accepted sample can leave `pending` unchanged. Rejected samples indicate emulator input or recording capacity/ordering failure, not guest omissions.

`guard` reports a nominal `due` and later guarded `deadline` for a queued transition; it can also report a CPU boundary that crossed into the guard. Repeated identical notices may be coalesced. `irq` is a modeled CPU interrupt-level change; `irq_accept` is the CPU callback when it begins accepting an interrupt, with the accepted level in `value`. SCC acknowledgement is visible as the applicable `scc_write`, not implied by CPU acceptance. Bus rows carry the mapped `address`, register and returned or written `value`. Device snapshots are taken after their event: an IRQ row can precede the corresponding `phase` row at the same cycle because changing the phase updates IRQ before the phase observation is appended.

Recordings contain accepted samples and timing settings; replay source rows are labelled 3. Repeatability requires the same ROM, media, emulator build, inputs and settings, and a comparable stop condition. Timeouts, additional scheduled input and different builds can change results. Matching replay demonstrates reproduction in BootHush only. Trace output fails if its new path cannot be created; the process result reports that failure.

## CPU interrupt-mask observations

Detailed witnesses (`BHMACMOUSE2` and `BHMACMOUSE3`) also include `cpu_mask`.
The first instruction boundary sampled inside the inclusive range establishes
a baseline; subsequent rows report changes. `value` is the current SR IPL
(0..7), and `reg` is the previous observed IPL, or 8 for that first baseline.
`address` is the PC captured before the CPU execute operation. A change seen
after that operation can include interrupt entry or exception handling; it is
not a claim that the instruction at that address alone changed SR. `pc` keeps
its existing instruction-context meaning. Legacy V1 output is unchanged.

The observer uses the CPU's side-effect-free public `getSR()` accessor before
and after execute. It does not read guest memory or devices, and it does not
change SR. Changes within one execute operation that return to the same IPL
are not visible. These are instruction-boundary intervals, not cycle-exact
hardware masking times. The first and last spans are censored by the selected
range, and the existing shared row cap applies. Dropped rows invalidate an
assumption that all mask changes were retained.

`scripts/probe_mac_cpu_mask.py` compares complete stopped JSON and framebuffer
with and without tracing, pins the supplied ROM hash, retains exact commands,
and reports closed mask spans separately from censored boundaries. It refuses
an incomplete or overflowing trace. Supply input times after a separately
observed guest readiness point; the harness does not infer readiness or the
meaning of inspected guest memory. All runs are bounded and headless.

Original unit fixtures cover explicit SR changes, inclusive filtering, stable
mask suppression, unchanged legacy output, interrupt entry and RTE. With the
Windows active-profile executable SHA-256
`f84de40301b433264fb3697bb2dcd132cdeb901a974d416e8d4482c81e73495d`,
the witness suite passes 11 tests; two optional external-fixture cases are
skipped. Log: `C:/Repos/.task-temp/boothush-mask-active-20261116-a/witness-tests-final.log`.
The observer uses the existing Moira public accessor declaration,
not its implementation; no third-party CPU code was copied or changed.

The ClasSICK and port results are indexed in their canonical receipts:
`C:/Repos/ClasSICK-0.97/docs/development/windows-m68k-toolchain-20261114.md`
and `C:/Repos/Not-Super-Tilt-Bro/docs/mouse-title-evidence.md`. In the latter,
title polling remains at IPL7 during the selected range: mask tracing alone
does not diagnose a polling loop's sampling losses. In ClasSICK, symbol-matched
SCC-handler spans exceed the fast schedule's DCD-change interval. This helps
target the next guest experiment; neither result validates physical timing
or establishes a sole cause.

## Filtered guest RAM writes

`--mouse-witness-memory ADDRESS:LENGTH` requires `--mouse-witness` and `--mouse-witness-detail 1`. It selects `BHMACMOUSE3` only for that request; `BHMACMOUSE1` and `BHMACMOUSE2` remain unchanged otherwise. The single byte range is in physical RAM offsets from `0` through `0x1ffff`, with positive length and an exclusive end no greater than `0x20000`. Invalid or repeated options fail before running. It observes CPU writes that successfully reach RAM; it neither reads MMIO nor adds guest reads or writes. The startup RAM overlay at bus `0x600000` maps to the same physical offsets as low RAM. Each event carries its actual CPU bus `address` and a separate `ram_offset` so aliases can be distinguished.

The header extends the V2 fields with the selected `RAM_FIRST RAM_LENGTH`, followed by `RAM_MATCHED RAM_DROPPED`:

```text
BHMACMOUSE3 FIRST LAST STOP COUNT DROPPED NO_DROPS RANGE_REACHED EDGE GUARD INVERT RAM_FIRST RAM_LENGTH RAM_MATCHED RAM_DROPPED
```

`RAM_MATCHED` counts RAM write transactions overlapping the address and cycle ranges, including dropped rows. `RAM_DROPPED` counts how many of those could not fit in the shared 65,536-row buffer; it is a subset of total `DROPPED`. Values outside either filter are intentionally excluded, not dropped. `NO_DROPS` and `RANGE_REACHED` have the V2 meanings; neither establishes that scheduled inputs were delivered at an exact stop boundary. At an early fault or stop, inspect the process status and `STOP` as well.

Every V3 row adds `ram_offset width captured_offset captured_width captured_value`; these fields are zero for non-RAM events. A `ram_write` is emitted **after** the modeled write. `width` is the observed bus-access width in bytes, and `captured_offset` is the offset within that access of the first byte inside the filter. `captured_width` is the number of overlapping bytes; `captured_value` is their unsigned big-endian value in decimal. Only overlapping bytes are reported, even when a transaction also touches RAM outside the filter. For a 68000, Moira normally presents a longword write as two ordered 16-bit writes. Consequently, reconstructing a 32-bit value requires both writes and care about intervening events; it is not guaranteed to be an atomic observation. `pc` is the existing instruction-context value, and event sequence resolves equal-cycle order. Captured writes are guest RAM activity, not proof of the meaning of any guest variable without the matching guest source and layout.

## ClasSICK timing investigation

With the separately supplied ClasSICK keyboard candidate ROM SHA-256 `b3a50ad806b589fdc02a4de579e6d72700c9107fb273d9ca73026320bebed807` and BootHush executable SHA-256 `617c749601dd2866fcfe5a0b1fce81ae7984e150e62d5282fa87b4a8d22e6062`, an input of 32 pixels at cycle 60000000 was tested in each of four directions with DCD inversion 3 and VBL guard 4096. At 2048 modeled cycles per edge, all four ended one pixel in the opposite direction with 29 guest-reported omissions. At 4096 all four reached the expected endpoint with zero omissions. An additional sweep produced failures in all four directions at 2560, and expected endpoints with zero omissions at 3072 and 3584. At 8192 with guard zero, the positive-X case ended one pixel short and reported two omissions. These outcomes are for that guest and modeled scenario, not a safe physical-mouse timing envelope.

`scripts/probe_classick_mouse.py --emulator EXE --rom ROM --output NEW_DIRECTORY` reproduces paired traced and untraced cases for the 2048, 4096, and guard-zero scenarios, retaining frames, traces and a report. All nine pairs matched in complete guest JSON and framebuffer in the recorded run; the selected witness range was reached without drops. In one +X 2048 trace, the SCC latch was pending at cycle 60006190, an interrupt was accepted at 60008188, and the phase changed at 60008336 while the latch remained pending. This establishes the modeled event order, not whether guest code, the emulator model, or their interaction caused the missed motion. No hardware behavior or fix has been established.

## Windows-native ClasSICK candidate

The same probe accepts the new Windows-built original ROM SHA-256
`6b622b2b163b86acdd3de54d1f656082ac55874830ebc1d58c204d06f36ef65a`.
It first checks the guest's own `CS09` ready stage and fault word in a separate
cold run before scheduling input; `--input-cycle` can choose an explicit
bounded cycle. The initial attempt at 60,000,000 cycles was invalid for this
ROM: a separate check showed stage 4. The revised preflight rejects that early
schedule, and the nine-case run uses cycle 100,000,000 after observing stage 16
at 99,990,006 with fault zero. The earlier invalid output remains in
`C:/Repos/.task-temp/boothush-windows-mac-mouse-20261114-a`.

With executable SHA-256
`5d4f610e98cbb8b03476f28929d6e5c0f64b941aafac3c7d5b6ef12c76c7eeb3`,
all nine traced/untraced pairs matched in complete output and frame, with no
witness drops. At 2,048 cycles per edge the four directions each reported 24
omissions and moved four pixels opposite the requested direction; at 4,096 all
four reached the intended 32-pixel endpoint with zero omissions. The 8,192
guard-zero positive-X case also reached its endpoint. Commands, trace/frame
hashes and the readiness result are in
`C:/Repos/.task-temp/boothush-windows-mac-mouse-20261114-c/report.json`.
One intermediate run was made while the probe source was changing and is not
the cited source-identity result. This is a bounded model observation, not a
physical rate, human-input result, sole-cause diagnosis or release qualification.

The later ClasSICK paired runner compared guard values 0, 1,024, 2,048 and
4,096 at 2,048 modeled cycles per edge; all failed its first movement
checkpoint. At 4,096 cycles per edge with guard zero, its eight checkpoints
and replay passed. Its exact sources, commands and results are in
`C:/Repos/ClasSICK-0.97/docs/development/windows-m68k-toolchain-20261114.md`.
These are guest/model results and do not make a physical timing claim.

A later original ClasSICK scratch build serviced SCC motion before VIA/VBL
bookkeeping, but still failed the 2,048-cycle paired input scenario while
passing the 4,096-cycle run. Its source identities, exact modeled outcomes
and limitations are recorded in
`C:/Repos/ClasSICK-0.97/docs/development/windows-m68k-toolchain-20261114.md`.
This does not establish a physical timing limit or a fix.

On 2026-11-15 a separate ClasSICK scratch build coalesced its foreground
diagnostic snapshots to examine interrupt-exclusion overhead. It and the
unchanged control still failed the 2,048-cycle modeled input case, while both
passed the 4,096-cycle paired scenario. The experiment was not adopted; source
identities, exact results and limits are recorded in the linked ClasSICK
evidence. This observation does not identify a sole cause or physical timing.

### Direct counter trace

On 2026-11-15 the probe gained `--witness-memory ADDRESS:LENGTH`; its default
remains the published-counter range `0xc1c:4`. It validates the range before
creating output, checks that each trace header names the requested range,
and verifies the emulator executable's identity remains unchanged. An invalid
`0x1ffff:2` range was rejected before output creation. The original default
and direct-counter paths each completed nine traced/untraced comparisons.

For the exact Windows-built ROM `6b622b2b...`, ClasSICK's own linked symbol and
compiled structure offset identify the internal omission counter at `0xb80:4`.
That address is specific to that ROM and profile. The derivation, image and
compiler identities are in
`C:/Repos/ClasSICK-0.97/docs/development/windows-m68k-toolchain-20261114.md`.
Reproduce with a fresh output directory:

```powershell
$py = 'C:/Users/segab/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$bh = 'C:/Repos/.task-temp/boothush-pc-hard-fault-twins-20261114-c/a/boothush.exe'
$rom = 'C:/Repos/.task-temp/mac-windows-os-speed-control-20261114-a/Os-a/classick-loaded-app.rom'
& $py -B scripts/probe_classick_mouse.py --emulator $bh --rom $rom --output C:/Repos/.task-temp/NEW-DIRECT-TRACE --witness-memory 0xb80:4
```

In `C:/Repos/.task-temp/boothush-mac-direct-omissions-20261115-b/report.json`,
each 2,048-cycle direction recorded 48 ordered 16-bit writes corresponding
to 24 longword counter updates. The four 4,096-cycle cases recorded none;
traced and untraced outputs matched and all selected witness ranges completed
without drops. In the +X fast trace, SCC pending was set at cycle 100006154,
another phase arrived at 100008214 before interrupt acceptance at that same
boundary, and the first internal counter write occurred at 100017216. Before
that write the guest's collected SCC A status changed from 12 to 4 across its
acknowledgment. This ordered model observation does not establish physical
bus timing, a hardware rate limit, or which component is solely responsible.

A subsequent ClasSICK idle-foreground polling experiment also failed the
2,048-cycle paired case and passed at 4,096; it was not adopted. Its source,
model commands and limits are in the linked Windows toolchain evidence.
