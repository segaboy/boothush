# ClasSICK 0.97 headless integration

BootHush is a Windows command-line test runner. The local checkout contains the current interface and tests. The usual built executable on this machine is `C:\Repos\boothush\out\windows\boothush.exe`. A checkout and an executable are separate artifacts. Check the source revision with `git -C C:\Repos\boothush rev-parse HEAD`, inspect the executable with `--help` and `--version`, and use `Get-FileHash` to identify its bytes. Rebuild it from the checkout when the source or build provenance is uncertain. Build instructions and the complete command reference are in the [README](../README.md).

Normal runs are headless and return one JSON result. When launching from Python with redirected output, use `creationflags=subprocess.CREATE_NO_WINDOW` to keep the console hidden. `--window-mouse` uses an invisible owned window with injected messages. `--live` opens a visible window for an explicitly selected interactive session. Neither requires control of unrelated Windows applications.

Every guest run requires `--machine`, `--cycles`, and `--timeout-ms`. Each guest starts from reset. A PC may use an original raw floppy or hard disk and the built-in firmware. A Mac requires a 65,536-byte ROM. ROMs, media, and generated output files stay outside the repository. Snapshot and trace outputs require new paths.

## ClasSICK x86-64 status

BootHush does not yet boot or execute the 64-bit ClasSICK port. The 286 profile cannot run it. A separate read-only, headless `--verify-x64-media` command checks the supplied GPT/FAT32 image and its embedded PE32+ EFI payload; it is not a guest run. See [x64 enablement](classick-x64.md) for an exact command, supported media, results, and remaining work.

An independently tested, opt-in [PS/2 auxiliary controller model](ps2-auxiliary.md) covers the current ClasSICK startup exchange and mouse reports. It is a device component, not a connected x64 machine or a guest execution test.

An independent x64 page-allocation and memory-map-key state model has synthetic stale-key and exit tests. It is not yet callable by an EFI guest; see the [x64 notes](classick-x64.md).

## Correlate the Mac mouse timing

The opt-in `--mouse-witness-detail 1` extends the existing bounded witness with scheduled and accepted input, pending transitions, guard deadlines, CPU interrupt acceptance and instruction context. It retains the existing witness by default and works headlessly with record/replay. See the [correlated mouse witness guide](mac-mouse-witness.md) for exact 2,048- and 4,096-cycle commands, field definitions and limits. A transition while an SCC interrupt is pending is an observation in this model, not a diagnosis or physical-hardware timing result.

## Inspect several regions at one stop

Repeat `--inspect` for each physical range. Up to 16 ranges are allowed, with 4,096 bytes per range and 16,384 bytes total. Overlapping ranges are returned independently in request order. Reads use side-effect-free diagnostic access to Mac RAM or PC conventional RAM and its video aperture. A single range returns the established `memory` object. Multiple ranges return `memory_ranges`, whose entries each contain `address` and hexadecimal `bytes`.

```powershell
$exe = 'C:\Repos\boothush\out\windows\boothush.exe'
$disk = 'C:\ClasSICK\boothush-headless-20261112-a\pc286-span-final\baseline.img'
& $exe --machine pc286 --floppy $disk --cycles 8000000 --timeout-ms 20000 `
  --until-post 0xa6 --inspect 0x20000:6 --inspect 0x30000:6
```

The retained ClasSICK diagnostic reached POST `0xa6` at cycle 48,849. The two ranges returned `00000044aabb` and `ccdd88000000` in that run. Other builds may have different outcomes.

## Start input when the guest is ready

`--input-after ADDRESS:WIDTH:VALUE` uses a side-effect-free physical-memory check. The first matching CPU boundary becomes offset zero for the scheduled inputs. It accepts direct scheduled mouse events and invisible `--window-mouse` events. An unreached condition at the cycle limit returns `readiness_not_reached`. Timeout and cancellation retain their own statuses. The result includes `input_schedule.ready`, `ready_cycle` when available, and scheduled and accepted counts. The mouse result includes delivered steps and pending transitions.

```powershell
$exe = 'C:\Repos\boothush\out\windows\boothush.exe'
$rom = 'C:\ClasSICK\mac-phase-witness-20261030-a\ci-artifacts\classick-window-live-pair-divisor4-mac\Os-a\classick-loaded-app.rom'
$trace = Join-Path $env:TEMP ('ready-' + [guid]::NewGuid().ToString('N') + '.bhmouse')
& $exe --machine mac128k --rom $rom --cycles 90000000 --timeout-ms 30000 `
  --input-after 0xc30:4:16 --mouse-dcd-invert 3 --mouse-motion 0:32:0 `
  --mouse-record $trace --inspect 0xb00:1024
& $exe --machine mac128k --rom $rom --cycles 90000000 --timeout-ms 30000 `
  --mouse-replay $trace --inspect 0xb00:1024
```

This ROM's SHA-256 is `b6b9072e7f034d1839a15f6782ed6870e2721fb6677eef320201ef772c506f2f`. Readiness occurred at cycle 77,711,848 in the checked run. Readiness addresses and expected values belong to the specific guest build. The recording stores accepted absolute guest cycles and mouse timing parameters. Use the same ROM, media and separately scheduled keyboard input for replay.

The [Mac mouse runbook](classick-097-mouse.md) includes a complete background sequence with two held drags, releases, movement after release, a content click, and fixture-specific diagnostic fields. It also describes the separate visible-window workflow.

## Trace 286 execution

The optional witness records CPU steps and hardware interrupt acknowledgements. Supply memory and port filters to include bus accesses, and use a cycle range to limit volume. The file header reports the stop cycle, saved event count and dropped event count. At most 65,536 events are retained. See the [README witness description](../README.md#local-process-api) for timing granularity and exception limitations.

```powershell
$exe = 'C:\Repos\boothush\out\windows\boothush.exe'
$disk = 'C:\ClasSICK\boothush-headless-20261112-a\pc286-span-final\baseline.img'
$witness = Join-Path $env:TEMP ('pc286-' + [guid]::NewGuid().ToString('N') + '.txt')
& $exe --machine pc286 --floppy $disk --cycles 8000000 --timeout-ms 20000 `
  --until-post 0xa6 --inspect 0x20000:6 --inspect 0x30000:6 `
  --pc-witness $witness --pc-witness-memory 0x20000:0x10006 --pc-witness-io 0x80:1
```

The retained diagnostic produced the same stopped state with and without the witness. Its checked witness held 10,200 events with zero dropped.

## Display and remaining limits

Both existing machines can record a bounded headless Y4M video, then use the bundled FFmpeg to make GIF, MP4 or PNG without relying on a system installation. See the [capture guide](headless-capture.md) for complete commands, bundle setup, timing and limits. The x64 profile is not implemented and cannot yet produce frames.

Mac captures use PBM. PC captures use RGB PPM. EGA graphics capture is available, and `--ega-font PATH` enables a fixed 80×25 text snapshot using a caller-supplied 3,584-byte font. The [EGA reference](ega.md) defines layout and limitations. The [PC/AT profile](pc-at.md) describes 286 hardware and media.

Mac floppy images are currently read-only. The [Mac floppy notes](mac-floppy.md) describe the missing write path. Mac mouse edge spacing and the VBL guard are configurable delivery controls. The model and its timing have not been established against a physical Macintosh. An emulator run alone does not establish physical-hardware behavior or historical System 1 compatibility.

The checked Mac and PC fixtures live in local ClasSICK directories and are optional inputs to BootHush tests. Existing ClasSICK scripts retain their current interface. Update them when they should consume multiple ranges, readiness-relative schedules, or the 286 witness. Run `ctest` with `BOOTHUSH_CLASSICK_LIVE_ROM` set to the intended pinned fixture and inspect its explicit fixture result when verifying the Mac integration.
