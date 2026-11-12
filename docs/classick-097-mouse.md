# ClasSICK 0.97 Mac mouse runbook

Use this workflow on the Windows machine with the local BootHush checkout and a 64 KiB ClasSICK Mac ROM. The checked fixture is the anchored candidate from November 12, 2026. The diagnostic layout and expected coordinates below apply to that binary. When working on a new ROM, confirm its layout against its source or locate the record tags in RAM.

## Check the local build and ROM

```powershell
$build = 'C:\Repos\boothush\out\windows'
$exe = Join-Path $build 'boothush.exe'
$rom = 'C:\ClasSICK\mac-candidate-anchored-20261112-a\classick-live.rom'
Get-Item $exe, $rom
(Get-FileHash $rom -Algorithm SHA256).Hash
```

The fixture hash is `faf37c6f96f8eb2ee6cf7a1e58a8b1b9538055d2fc89493c7419eeea816f6c8c`. Follow the [build instructions](../README.md#build-and-test) if the executable or tests are missing. Set `$build` and `$exe` to the resulting paths. Keep the ROM outside the BootHush repository.

## Run the automated check

```powershell
$env:BOOTHUSH_CLASSICK_LIVE_ROM = $rom
ctest --test-dir $build -R '^mac_live$' -V
```

Look for `ClasSICK live ROM accepted corners, drags and a click.` The test executable also runs with the fixture unset, so a CTest pass alone is insufficient. The fixture test drives an invisible BootHush window with Windows mouse messages and verifies these guest outcomes.

| Action | Expected result |
| --- | --- |
| Move to each corner | Guest pointer reaches `(0,0)`, `(511,0)`, `(511,341)`, and `(0,341)`. The corner frame hashes match the fixture. |
| Drag the title bar 32 pixels right, then 24 pixels up in a second drag | Guest window moves from `(80,70)` to `(112,70)` to `(112,46)`. |
| Move after releasing each drag | Pointer moves and the window stays at its last position. The primary button reports released. |
| Click the content area | Content pattern toggles to `1`. The guest counts three presses across the two drags and click. |
| Inspect counters | Hard loss and omissions are zero. The fixture reports 2,279 accepted merges. |

This regression uses injected window messages. A physical mouse run is a separate check of how Windows delivers ordinary mouse input.

## Record a physical mouse run

For a background injected-window test using the same ROM and no visible window, run the following. The coordinates are positions in BootHush's 512×342 input viewport. The first event anchors the window pointer. Button actions use logical primary messages, independently of the Windows swapped-button setting.

```powershell
$trace = Join-Path $env:TEMP ('boothush-' + [guid]::NewGuid().ToString('N') + '.bhmouse')
$result = & $exe --machine mac128k --rom $rom --cycles 205000000 --timeout-ms 20000 --mouse-dcd-invert 3 --mouse-record $trace --inspect 0xb00:1024 `
  --window-mouse 100000000:256:171:0 --window-mouse 108000000:100:80:0 `
  --window-mouse 120000000:100:80:1 --window-mouse 128000000:132:80:2 `
  --window-mouse 136000000:132:80:3 --window-mouse 144000000:148:80:0 `
  --window-mouse 152000000:148:80:1 --window-mouse 160000000:148:56:2 `
  --window-mouse 168000000:148:56:3 --window-mouse 176000000:148:64:0 `
  --window-mouse 184000000:150:110:0 --window-mouse 192000000:150:110:1 `
  --window-mouse 192000000:150:110:3 | ConvertFrom-Json
$replay = & $exe --machine mac128k --rom $rom --cycles 205000000 --timeout-ms 20000 --mouse-replay $trace --inspect 0xb00:1024 | ConvertFrom-Json
$result.input_source
$result.mouse
$result.memory.bytes -ceq $replay.memory.bytes
```

The verified candidate reaches pointer `(150,110)`, window `(112,46)`, three presses, released button, pattern `1`, and zero pending transitions. `input_source` reports `injected_window_messages`. This exercises the owned window procedure and guest but does not use operating-system mouse delivery or a physical device. A controller can launch BootHush with `CREATE_NO_WINDOW` to keep its console hidden.

The following section describes a separately requested interactive session that opens a visible window.

```powershell
$capture = Join-Path $build ('mouse-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $capture | Out-Null
$trace = Join-Path $capture 'live.bhmouse'
$live = & $exe --machine mac128k --rom $rom --live 2 --mouse-dcd-invert 3 --cycles 440000000 --timeout-ms 60000 --mouse-record $trace --inspect 0xb00:1024 --frame (Join-Path $capture 'live.pbm') | ConvertFrom-Json
```

Activate BootHush and wait for the guest desktop, about eight seconds on the fixture at live speed. Move the guest pointer toward the corners, drag the title bar in two directions, move again after releasing, and click the window content. Let motion settle before closing. The window can remain open for about 56 seconds at the sample cycle cap. Confirm `$live.mouse.samples` is nonzero and `$live.mouse.pending` is zero for a completed gesture. The `closed` status is expected when you close the window.

```powershell
$replay = & $exe --machine mac128k --rom $rom --mouse-replay $trace --cycles 440000000 --timeout-ms 10000 --inspect 0xb00:1024 --frame (Join-Path $capture 'replay.pbm') | ConvertFrom-Json
$live.cycles -eq $replay.cycles
$live.memory.bytes -ceq $replay.memory.bytes
(Get-FileHash (Join-Path $capture 'live.pbm')).Hash -eq (Get-FileHash (Join-Path $capture 'replay.pbm')).Hash
```

A replay to the recorded end reports `cycle_limit`. Inspect the input cycles near the failed gesture in `live.bhmouse`, then add `--mouse-witness PATH --mouse-witness-range FIRST:LAST` to a replay using a new output path. Use the [device trace fields](../README.md#device-trace) to compare pin changes with the guest's SCC/VIA reads and IRQ transitions.

## Read the candidate's diagnostic RAM

`--inspect 0xb00:1024` includes these records. Each field below is a big-endian 32-bit word. Word index `0` is the four-byte record tag.

| Tag and base address | Words to check |
| --- | --- |
| `CS09` at `0xCA8` | `1` stage (`16` when ready), `3` primary press count, `4` trap count (`1` in this fixture), `6` guest fault (`0`). |
| `MWP1` at `0xC28` | `1` version (`2`), `2` size (`128`), `8` baseline count, `9` hard losses, `10` queued guest motion, `12` deliveries, `13–14` pointer X/Y, `15–16` window X/Y, `17` content pattern, `22` button held (`0` after release). |
| `MLC1` at `0xC04` | `1–4` loss counters. The passing fixture leaves all four at zero. |
| `MMD1` at `0xC18` | `1` omissions. |
| `MCM1` at `0xC20` | `1` accepted coalescing. Compare it alongside omissions and the resulting coordinates. |

For a quick headless readiness check on this exact ROM, run `& $exe --machine mac128k --rom $rom --cycles 80000000 --timeout-ms 10000 --until 0xc48:4:1 --inspect 0xb00:1024`. Expect `condition_met` when `MWP1` records its first baseline. The [fixture test source](../tests/test_mac_live.cpp) contains the exact waypoint sequence and expected framebuffer hashes.
