# ClasSICK 0.97 headless integration

ClasSICK is BootHush's immediate integration priority. BootHush's broader goal is headless development and testing of portable software across platforms; see the [project direction](project-direction.md).

BootHush is a Windows command-line test runner. The local checkout contains the current interface and tests. The usual built executable on this machine is `C:\Repos\boothush\out\windows\boothush.exe`. A checkout and an executable are separate artifacts. Check the source revision with `git -C C:\Repos\boothush rev-parse HEAD`, inspect the executable with `--help` and `--version`, and use `Get-FileHash` to identify its bytes. Rebuild it from the checkout when the source or build provenance is uncertain. Build instructions and the complete command reference are in the [README](../README.md).

Normal runs are headless and return one JSON result. When launching from Python with redirected output, use `creationflags=subprocess.CREATE_NO_WINDOW` to keep the console hidden. `--window-mouse` uses an invisible owned window with injected messages. `--live` opens a visible window for an explicitly selected interactive session. Neither requires control of unrelated Windows applications.

Mac and PC guest runs require `--machine`, `--cycles`, and `--timeout-ms`. Each guest starts from reset. A PC may use an original raw floppy or hard disk and the built-in firmware. A Mac requires a 65,536-byte ROM. The separate experimental [NES target](nes.md) uses instruction-bounded `--steps`, two scheduled controllers and stopped CPU/PPU/OAM observations for porting research. ROMs, media, and generated output files stay outside the repository. Snapshot and trace outputs require new paths.

## Current Mac/286 direction

The owner's 2026-11-14 direction makes native Mac128K and PC/AT 286 the next paired-release targets; x64 work is paused. The [original EGA/keyboard diagnostic](pc286-pair-probe-20261114.md) checks a centered 512x342 canvas and two directional cursor moves with scheduled keys. It does not run ClasSICK or establish a window, parity, human input or physical-hardware behavior. See the [PC/AT profile](pc-at.md) for exact firmware and keyboard limitations.

ClasSICK's separate four-track diagnostic loader test now exercises injected
read failure at each track boundary as well as its successful reads and a
short-read control. Commands, media hashes and observations are in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
These checks do not qualify a physical BIOS or a production boot loader.

A separate [keyboard-controlled frame experiment](pc286-keyboard-drag-20261114.md) checks an original rectangular frame through a toggled press, move, release and ignored post-release movement. It tests this modeled device path with synthetic input, not ClasSICK window behavior or physical dragging.

The [Mac-sized bitmap comparison](mac-pc286-bitmap-20261114.md) loads a caller-supplied original PBM into an original 286 test guest and compares all EGA canvas and margin pixels. It is useful for cross-project display experiments, but does not run a ClasSICK renderer or establish behavioral parity.

Not Super Tilt Bro's experimental mouse-title-to-keyboard-play ROM is another headless Mac integration consumer. Its canonical receipt is `C:/Repos/Not-Super-Tilt-Bro/docs/mouse-title-evidence.md`: it records the exact ROM and executable hashes, bounded input schedules, keyboard-fault retry, guest observations and a controlled comparison of mouse-edge intervals. In that particular guest/model combination, delivery of all scheduled transitions did not guarantee that the guest tracked them accurately. Set `BOOTHUSH_STB_TITLE_ROM` to the specifically pinned caller-supplied ROM to opt into the repeatable [integration test](mac-keyboard.md#optional-port-title-integration); it skips if unset. The port's game-informed implementation is not admitted as a ClasSICK input, and the result neither diagnoses a BootHush defect nor qualifies physical hardware or human input.

The current source-matched Windows-built ClasSICK Mac ROM and 286 diagnostic
also match at eight scripted state and pixel checkpoints in BootHush, with a
replayed Mac trace. Faster modeled mouse input still fails; the ready-checked
four-direction investigation and exact identities are in
[the mouse witness guide](mac-mouse-witness.md#windows-native-classick-candidate)
and `C:/Repos/ClasSICK-0.97/docs/development/windows-m68k-toolchain-20261114.md`.
The port separately exercises a Windows-compiled native-C movement fixture;
its [optional integration](mac-keyboard.md#optional-port-native-c-fixture)
is not game completion or ClasSICK implementation input.

## Preserved x86-64 status

BootHush does not yet boot the 64-bit ClasSICK port. The 286 profile cannot run it. A separate read-only, headless `--verify-x64-media` command checks the supplied GPT/FAT32 image and its embedded PE32+ EFI payload; it is not a guest run. The experimental software probe can interpret a limited number of actual EFI instructions without a hypervisor, but does not provide a UEFI context or boot. See [x64 enablement](classick-x64.md) for commands, results, and remaining work.

The separate `--map-x64-media` command now builds and relocates the disk's actual EFI image in host memory, with independently tested mapping bytes and explicit `executed:false`. It does not create a virtual machine or run firmware.

An independently tested, opt-in [PS/2 auxiliary controller model](ps2-auxiliary.md) covers the current ClasSICK startup exchange and mouse reports. It is a device component, not a connected x64 machine or a guest execution test.

An independent x64 page-allocation and memory-map-key state model has synthetic stale-key and exit tests. It is not yet callable by an EFI guest; see the [x64 notes](classick-x64.md).

A separate host-callable x64 experiment serializes that model into bounded byte-buffer descriptors and exercises EFI-style allocation, map, and exit responses. It remains disconnected from a CPU and does not constitute a complete firmware memory map or a guest boot. [Scope and evidence](x64-efi-memory-evidence-20261114.md).

## Correlate the Mac mouse timing

The opt-in `--mouse-witness-detail 1` extends the existing bounded witness with scheduled and accepted input, pending transitions, guard deadlines, CPU interrupt acceptance and instruction context. Add `--mouse-witness-memory ADDRESS:LENGTH` to include filtered guest RAM writes in the same ordered stream. It retains the existing witness by default and works headlessly with record/replay. See the [correlated mouse witness guide](mac-mouse-witness.md) for exact 2,048- and 4,096-cycle commands, field definitions and limits. A transition while an SCC interrupt is pending is an observation in this model, not a diagnosis or physical-hardware timing result.

For keyboard timeout and malformed-byte paths, `--keyboard-fault` selects a single command occurrence and deterministic stall, missing, delayed, malformed, or disconnected response. Use it with `--keyboard-witness` and inspect fault, trace, input and guest diagnostic status. See [keyboard fault injection](mac-keyboard.md#deterministic-injected-keyboard-faults) for the modes, queue semantics, and pinned ClasSICK guest results.

## Inspect several regions at one stop

Repeat `--inspect` for each physical range. Up to 16 ranges are allowed, with 4,096 bytes per range and 16,384 bytes total. Overlapping ranges are returned independently in request order. Reads use side-effect-free diagnostic access to Mac RAM or PC conventional RAM and its video aperture. A single range returns the established `memory` object. Multiple ranges return `memory_ranges`, whose entries each contain `address` and hexadecimal `bytes`.

For a PC run with scheduled scancodes, `pc_keyboard_input` accounts for
scheduled delivery, and `pc_keyboard_buffer` snapshots the controller model's
output queue at stop. These are distinct queues and neither substitutes for
guest state inspection. See [PC/AT keyboard accounting](pc-at.md).

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

For scheduled 286 keyboard input, inspect `pc_keyboard_input` in the process
JSON. Its accepted count is model admission, while rejected and pending events
identify input that was not admitted before stop; guest state and output still
need separate checks. The source-matched ClasSICK shared-session runner now
checks these counts alongside its guest-state and full-pixel assertions. The
field's exact behavior and stop-boundary rule are in the [PC/AT guide](pc-at.md).

The built-in 286 firmware can deliberately fail a selected floppy read using `--fail-floppy-read C:H:S`. The two-track diagnostic loader begins its payload reads at `0:0:2` and `0:1:1`; choosing either exercises its error branch, while JSON records the number of injections. The [PC/AT guide](pc-at.md) defines status, matching, and limitations. This is a test facility, not a failure of ordinary disk reads.

The corresponding `--fail-hard-disk-read C:H:S` tests valid fixed-disk reads
in the built-in firmware. The newer ClasSICK whole-disk diagnostic has been
checked at each of its actual linked payload reads; see
[hard-disk fault evidence](pc286-hard-disk-fault-20261114.md). This remains a
modeled fault, not a physical or production-loader qualification.
The matching `--fail-hard-disk-write` option tests write refusal; the
generated-disk copy experiment distinguishes a failed write from a failed
read after a completed write, which does not roll back the disk.
The `--fail-hard-disk-read-once` variant injects one matching failure so an
original guest can exercise a subsequent reset and retry; see the [PC/AT
guide](pc-at.md). It models no physical media recovery behavior.

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

ClasSICK's subsequent original 286 diagnostic can also generate a disposable
whole hard-disk image for this model and run its shared window session from
it. Its host and Mac comparisons, disk identity, boundary controls and
source/specification records are in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
It is not an installer or evidence of physical disk geometry or BIOS behavior.

The current [active-profile twin build](active-profile-build-20261115.md)
rebuilds the Mac, 286 and NES components while retaining the separately
identified prebuilt x64 media dependency. Its paired integrations and source
boundaries are recorded there; it is not a distribution-qualified build.

## Display and remaining limits

Both existing machines can record a bounded headless Y4M video, then use the bundled FFmpeg to make GIF, MP4 or PNG without relying on a system installation. See the [capture guide](headless-capture.md) for complete commands, bundle setup, timing and limits. The x64 profile is not implemented and cannot yet produce frames.

Mac captures use PBM. PC captures use RGB PPM. EGA graphics capture is available, and `--ega-font PATH` enables a fixed 80×25 text snapshot using a caller-supplied 3,584-byte font. The [EGA reference](ega.md) defines layout and limitations. The [PC/AT profile](pc-at.md) describes 286 hardware and media.

Mac floppy images are currently read-only. The [Mac floppy notes](mac-floppy.md) describe the missing write path. Mac mouse edge spacing and the VBL guard are configurable delivery controls. The model and its timing have not been established against a physical Macintosh. An emulator run alone does not establish physical-hardware behavior or historical System 1 compatibility.

The checked Mac and PC fixtures live in local ClasSICK directories and are optional inputs to BootHush tests. Existing ClasSICK scripts retain their current interface. Update them when they should consume multiple ranges, readiness-relative schedules, or the 286 witness. Run `ctest` with `BOOTHUSH_CLASSICK_LIVE_ROM` set to the intended pinned fixture and inspect its explicit fixture result when verifying the Mac integration.
