# Correlated Mac mouse witness

`--mouse-witness-detail 1`, together with `--mouse-witness PATH`, opts into the `BHMACMOUSE2` format. Without this option the original `BHMACMOUSE1` format is unchanged. The witness is disabled by default. It is an observation of BootHush's model, not a physical timing measurement or guest-owned omission counter. It does not write to guest memory or control the Windows mouse or keyboard. The native invisible-window fixture sends window messages; it is not physical input.

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
    --mouse-witness $witness --mouse-witness-detail 1 `
    --mouse-witness-range 60000000:60500000
  Write-Output "Witness: $witness"
}
```

For this exact ROM, the big-endian 32-bit guest-owned pointer coordinates are at `0xc5c` and `0xc60`, and the guest omission counter is at `0xc1c` within the inspected range. These addresses are not general Mac interfaces. Replace the motion argument with `60000000:-32:0`, `60000000:0:32`, or `60000000:0:-32` to trace the other directions. The optional `BOOTHUSH_CLASSICK_LIVE_ROM` test covers all four at both intervals.

In the reproduced +X case, the 2,048-cycle trace shows the X phase at 2 when SCC interrupt level 2 became pending at cycle 60,006,236; the X phase changed to 0 at 60,008,286 while SCC pending remained set; the CPU accepted level 2 at 60,008,554. The 4,096-cycle case reached the expected final position and the 2,048-cycle case ended one pixel left with 29 guest-reported omitted passes. Both delivered 64 modeled transitions and ended with zero pending. In separate paired runs, enabling the witness left the stopped guest JSON and full framebuffer identical. These observations support investigating the relationship between a latched SCC signal and a later VIA read; they do not establish whether the guest, emulator, or their interaction is defective. A useful next test is to correlate per-pass guest-owned diagnostic writes with the SCC latch/acknowledgement and VIA sampling, then compare behavior under a separately validated timing model.

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
