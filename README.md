# BootHush

The [EGA guide](docs/ega.md) includes an original diagnostic-font generator for
readable 286 installer/error captures without a firmware font.

BootHush is a headless development and testing platform for portable software and ports between different platforms. Its goal is to let developers build a repeatable test, run it against supported source and target environments, and compare behavior while they adapt the software. See the [project direction](docs/project-direction.md) for the intended workflow and current boundaries.

ClasSICK is the immediate development priority and the project exercising BootHush today. The current implementation runs on Windows and provides Mac 128K and 286 machine profiles. Guest invocations load local images, run an isolated guest, return a JSON result, and exit. Runs are headless by default. The Mac profile also has an opt-in window for live mouse testing and recording.

ClasSICK developers can start with the [headless integration guide](docs/classick-integration.md) for the current Mac and 286 interfaces, runnable examples, and limitations. Read the local checkout to discover changes committed there.

The [x64 enablement notes](docs/classick-x64.md) describe the headless ClasSICK UEFI goal and current development tools. **A complete x86-64 CPU, UEFI firmware, and x64 guest boot are not yet implemented.**

The experimental [NES target](docs/nes.md) runs supported NROM and NES 2.0 mapper-2/submapper-1 cartridges headlessly, with NTSC/PAL timing models, independent scheduled input for both controllers, cycle-aligned input and memory observations, and approximate PPU and audio capture. CPU, graphics, audio and cartridge compatibility remain incomplete.

The [capture guide](docs/headless-capture.md) describes bounded, indexed NES and Mac frame series. The NES profile provides a filtered execution witness, while the Mac profile has opt-in [keyboard](docs/mac-keyboard.md) fault injection and keyboard and display witnesses. The small [scenario comparison helper](docs/scenario-comparison.md) compares explicitly mapped captured values and guest times.

Other Windows applications can embed that same core using the experimental [NES C library and API](docs/nes-api.md). The build installs a DLL, C header and CMake package; an independent consumer example is included.

`--map-x64-media DISK --load-base ADDRESS` additionally extracts the EFI file from the validated disk and constructs a relocated PE32+ image in host memory for inspection. It does not execute a guest.

`--probe-x64-software DISK --steps N` interprets a small, explicitly documented set of x64 instructions from the disk's EFI entry point without a hypervisor. This experimental probe provides only a synthetic stack and zeroed registers, not a valid UEFI context; it is not a boot. Unsupported instructions and memory faults stop visibly. See the [x64 notes](docs/classick-x64.md) for the exact limits and exit statuses.

An opt-in PS/2 auxiliary-device model has independent tests for ClasSICK's mouse startup and packet routing. It is not connected to a guest profile yet; see [PS/2 auxiliary model](docs/ps2-auxiliary.md).

An independent x64 boot-services state model tests page allocation and stale memory-map keys, but is not yet connected to guest firmware or execution.

`--x64-capability` reports a Windows Hypervisor Platform availability query without creating a virtual machine or executing a guest. It does not establish that a usable backend exists in BootHush.

| Profile | Supported test path |
| --- | --- |
| `mac128k` | Motorola 68000, 128 KiB RAM, 64 KiB ROM, Macintosh reset overlay, 512 × 342 monochrome framebuffer, VIA/SCC mouse and keyboard input, serial RTC/PRAM, and read-only single-sided 400 KiB IWM floppy media. Verified with ClasSICK replacement ROMs and an original 68000 floppy boot fixture. |
| `pc286` | 6 MHz Intel 80286 and 640 KiB conventional RAM, targeting the IBM PC/AT 5170 with 640 × 350 planar EGA, a read-only 1.2 MB floppy, and a 20 MB hard disk. Includes keyboard input, PIC/PIT/RTC, floppy DMA, fixed-disk PIO, and POST capture. Verified with original test ROMs and boot media. |

Macintosh System 1 and factory IBM BIOS compatibility require separate verification with the intended ROMs and media. Macintosh floppy writes, serial communications and sound remain open. See the [PC/AT boot profile](docs/pc-at.md) and [EGA profile](docs/ega.md) for device behavior and limits. Keep proprietary ROMs and test media outside the public repository.

## Build and test

Use CMake 3.21 or later with a Windows C11/C++20 toolchain. Install or select Python 3 for tests. The current Windows build was verified with LLVM-MinGW and Ninja. From the source checkout with the tools on PATH:

```powershell
$build = 'C:\ClasSICK\boothush'
cmake -S . -B $build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build $build
ctest --test-dir $build --output-on-failure
```

Choose a writable build directory. To build the executable without Python, add `-DBUILD_TESTING=OFF` when configuring. Default tests generate their own ROMs and disk images.

For the current Mac/286 development scope, the [active-profile twin build](docs/active-profile-build-20261115.md) rebuilds those components and NES while retaining the identified prebuilt x64 media dependency. It is an engineering build, not a source-complete distribution build.

The PC/AT model can inject a persistent or one-shot hard-disk read failure for bounded recovery tests; see the [PC/AT guide](docs/pc-at.md) for selection, status and limits.

- Set `BOOTHUSH_CLASSICK_ROM` to the local 64 KiB startup fixture with SHA-256 `be506ad6e41d06ebcfe11678a4f6a64ba560920b16efa7fef884794f51690005` to verify the original ROM’s diagnostic words and framebuffer checksum.
- Set `BOOTHUSH_CLASSICK_WINDOW_ROM` and `BOOTHUSH_CLASSICK_WINDOW_ROM_SHA256` for the short-collect invert-3 ClasSICK ROM with the CS09 and MWP1 v2 diagnostics. This exercises a scheduled press, release and four quadrature transitions, then checks guest motion and loss counters.
- Set `BOOTHUSH_CLASSICK_LIVE_ROM` to the 2026-11-12 anchored ClasSICK candidate with SHA-256 `faf37c6f96f8eb2ee6cf7a1e58a8b1b9538055d2fc89493c7419eeea816f6c8c`. The native mouse integration test sends messages to an invisible BootHush window and verifies the four corners, two title-bar drags, release, and a content click against the ROM's RAM diagnostics and framebuffer.
- Set `BOOTHUSH_FREEDOS_FLOPPY` to `120m/x86BOOT.img` from the [official FreeDOS 1.4 Floppy Edition](https://www.freedos.org/download/). The test verifies the boot image SHA-256, selects a language with a scheduled Enter, and checks the installer prompt.
- Set both `BOOTHUSH_CLASSICK_X64_DISK` and `BOOTHUSH_CLASSICK_X64_EFI` to enable the optional pinned ClasSICK x64 media inspection test. It validates and compares the media without running it.
- Set `BOOTHUSH_STB_UNROM` to the user-supplied, hash-pinned Super Tilt Bro beta ROM to exercise cycle-aligned controller and multi-frame capture scenarios. Its source revision is unknown; no ROM is distributed here.
- Set `BOOTHUSH_STB_MAC_PROBE` to the independently generated port controls ROM (SHA-256 `31b2666be66ac7a02d576998cb9b46aec3a91245d16679877d593748fd581fc6`) to verify keyboard delivery and VBL-indexed capture against the port's probe.
- Set `BOOTHUSH_CLASSICK_KEYBOARD_ROM` to the ClasSICK keyboard candidate (SHA-256 `b3a50ad806b589fdc02a4de579e6d72700c9107fb273d9ca73026320bebed807`) to run the guest's timeout and malformed-reply fault tests.

## Local process API

Run `boothush.exe` directly from an existing terminal. When a controller launches a child with redirected streams, use Windows `CREATE_NO_WINDOW`. Python’s `subprocess.run` accepts it as `creationflags=subprocess.CREATE_NO_WINDOW`.

```powershell
& "$build\boothush.exe" --machine mac128k --rom $rom --cycles 80000000 --timeout-ms 10000 --until 0xb08:4:1 --inspect 0xb00:28
& "$build\boothush.exe" --machine pc286 --floppy $installer --hard-disk $blankDisk --disk-out $newDisk --cycles 4000000 --timeout-ms 10000
```

Select one `--machine`. Macintosh requires a ROM and accepts a floppy. The PC accepts a system ROM with optional media, or uses built-in firmware to boot supplied media with floppy first. Both `--cycles` (1–4,000,000,000 emulated CPU cycles) and `--timeout-ms` (1–3,600,000 wall milliseconds for guest execution) are required. Cycle counts can cross the cap by the cost of one CPU step. Image loading and requested capture occur outside the guest execution timer. A process supervisor can also impose an end-to-end timeout. `--help` lists options and `--version` reports build date and time. Use `Get-FileHash` on the executable to identify exact binary bytes.

| Option | Behavior |
| --- | --- |
| `--rom PATH` | Read a 65,536-byte system ROM. PC mode maps it at the low and high BIOS ranges used during reset. |
| `--floppy PATH` | Mount read-only media. Macintosh accepts a raw 409,600-byte image or 400 KiB Disk Copy 4.2. PC accepts a raw 1,228,800-byte image. PC built-in firmware checks the boot signature and loads the selected sector at `0000:7C00`. |
| `--fail-floppy-read C:H:S` | PC built-in firmware test only: fail each valid floppy INT 13h read starting at the specified cylinder, head and sector. Requires `--floppy`, and cannot be used with `--rom`. Reports the selection and injection count in JSON. See [PC/AT](docs/pc-at.md). |
| `--fail-hard-disk-read C:H:S` | PC built-in firmware test only: fail each valid hard-disk INT 13h read starting at the specified cylinder, head and sector. Requires `--hard-disk`, and cannot be used with `--rom`. Reports the selection and injection count in JSON. See [PC/AT](docs/pc-at.md). |
| `--fail-hard-disk-write C:H:S` | PC built-in firmware test only: fail each valid hard-disk INT 13h write starting at the specified cylinder, head and sector. Requires `--hard-disk`, and cannot be used with `--rom`. Reports the selection and injection count in JSON. See [PC/AT](docs/pc-at.md). |
| `--hard-disk PATH` | PC only. Mount a raw 21,411,840-byte IBM type 2 disk (615 cylinders, 4 heads, 17 sectors). Guest writes go to the private in-memory copy. |
| `--disk-out PATH` | PC only. Save the resulting hard disk to a new raw image after a successful run. Use that output as `--hard-disk` to boot it on a subsequent run. |
| `--ega-rom PATH` | PC only, with a system ROM. Map a valid EGA option ROM at `0xC0000`. |
| `--until ADDRESS:WIDTH:VALUE` | Stop when 1, 2 or 4 physical memory bytes equal the value in guest byte order. |
| `--until-pc ADDRESS` | Stop at a 24-bit physical instruction address. |
| `--until-pc-after CYCLE` | With `--until-pc`, ignore earlier visits and stop at the first matching boundary at or after this absolute guest cycle. Mac/286 only; see [qualified PC stops](docs/pc-stop-cycle.md). |
| `--until-post BYTE` | PC only. Stop when the guest writes the value to I/O port `0x80`. |
| `--inspect ADDRESS:LENGTH` | Inspect physical memory at stop, without device side effects. Repeat up to 16 times, each 1–4,096 bytes, with at most 16,384 bytes total. Overlaps and duplicates are returned in request order. One range keeps the existing `memory` object. Multiple ranges use `memory_ranges`, an array of `{address, bytes}` objects. |
| `--frame PATH` | Write a new image at stop. Macintosh emits binary PBM and PC emits binary RGB PPM. Existing files are protected. |
| `--video PATH` | Record guest framebuffer samples headlessly as an uncompressed Y4M file. See [headless capture](docs/headless-capture.md) for GIF, MP4 and PNG conversion. |
| `--video-fps N` | Video sampling rate in guest time, 1–60 frames per second; default 10. |
| `--video-frames N` | Bound the recording to 1–3,600 frames; default 300. Guest execution continues after the cap. |
| `--video-start CYCLE` | Start sampling at an absolute guest cycle, default zero. Useful for skipping boot. |
| `--mouse-button CYCLE:STATE` | Macintosh only. Schedule button state `1` pressed or `0` released. |
| `--mouse-phase CYCLE:X:Y` | Macintosh only. Set raw two-bit quadrature phases. Bit 0 drives SCC DCD and bit 1 drives VIA PB4/PB5. Default phases are `2:2`. |
| `--mouse-motion CYCLE:DX:DY` | Macintosh only. Queue signed pixel deltas through the same paced quadrature bridge used for live input. Positive X is right and positive Y is down. Scheduled buttons join the same queue. |
| `--window-mouse CYCLE:X:Y:ACTION` | Macintosh only. Deliver a logical mouse event to an invisible owned window at a guest cycle. Actions are 0 for unpressed movement, 1 for primary press, 2 for held movement, and 3 for release. X is 0–511 and Y is 0–341. Events must be in cycle order. |
| `--live SCALE` | Macintosh only. Open a native mouse window at integer scale 1–4 and pace the guest to its nominal 7.8336 MHz clock. |
| `--mouse-record PATH` | With `--live`, `--window-mouse`, or `--mouse-motion`, write the accepted mouse samples and their guest cycles to a new trace file. |
| `--mouse-replay PATH` | Macintosh only. Replay a recorded trace headlessly with its saved mouse timing and polarity. The recorded end cycle caps the replay. |
| `--mouse-witness PATH` | Macintosh only. Write a device trace with applied mouse pins, SCC/VIA accesses, and IRQ changes to a new file. Works with live, replay, raw phases, or no input. |
| `--mouse-witness-range FIRST:LAST` | With `--mouse-witness`, capture only the inclusive guest cycle range. The default is cycle zero through the requested cycle limit. |
| `--mouse-witness-detail 1` | With `--mouse-witness`, select the extended `BHMACMOUSE2` format with input, guard, CPU interrupt acceptance, PC context and queue information. See [correlated mouse witness](docs/mac-mouse-witness.md). The original format remains the default. |
| `--mouse-witness-memory ADDRESS:LENGTH` | With the detailed Mac witness, add guest RAM writes overlapping the selected physical RAM offsets. This selects the `BHMACMOUSE3` format; filtered bytes, bus width, coverage and drop counts are documented in the [witness guide](docs/mac-mouse-witness.md). |
| `--input-after ADDRESS:WIDTH:VALUE` | Start a scheduled-input timeline when this physical-memory condition first matches. Scheduled cycle numbers become offsets from the first matching instruction boundary. Zero-offset events are applied at that boundary. Requires scheduled input and works headlessly with direct input or window messages. |
| `--pc-witness PATH` | PC only. Save a bounded 286 execution witness to a new file. |
| `--fail-hard-disk-read-at C:H:S:N` | PC built-in firmware only. Fail exactly the positive Nth valid read starting at the selected CHS; see [PC/AT](docs/pc-at.md). |
| `--fail-hard-disk-write-at C:H:S:N` | PC built-in firmware only. Fail exactly the positive Nth valid write starting at the selected CHS; see [PC/AT](docs/pc-at.md). |
| `--fail-hard-disk-reset-at N` | PC built-in firmware only. Fail exactly the positive Nth valid fixed-disk reset; see [PC/AT](docs/pc-at.md). |
| `--pc-witness-memory ADDRESS:LENGTH` | With `--pc-witness`, record byte bus reads and writes within the specified physical range. |
| `--pc-witness-io PORT:LENGTH` | With `--pc-witness`, record bus input and output within the specified port range. |
| `--pc-witness-cycles FIRST:LAST` | With `--pc-witness`, restrict recorded events to these inclusive cycle boundaries. |
| `--ega-font PATH` | PC only. Supply 3,584 bytes of caller-owned glyphs to enable the 80×25 EGA text snapshot described in [EGA diagnostics](docs/ega.md). |
| `--mouse-edge-cycles N` | With `--live` or `--mouse-motion`, set the minimum interval between quadrature transitions in guest cycles. The default is 8,192. |
| `--mouse-dcd-invert MASK` | With `--live` or `--mouse-motion`, flip direction for bit 0 (X) and bit 1 (Y). The default is `0`. The anchored ClasSICK candidate uses `3`. |
| `--mouse-vbl-guard N` | With `--live` or `--mouse-motion`, leave `N` guest cycles on either side of each vertical blank before applying a transition. The default is 4,096. Use `0` to exercise unrestricted VBL timing. |
| `--key CYCLE:CODE:STATE` | Macintosh only. Queue an M0110 scan position `0x00`–`0x39`, with `1` pressed and `0` released. |
| `--scancode CYCLE:BYTE` | PC only. Queue one raw keyboard Set 2 byte. For a release, schedule `0xF0` and then the key byte. |
| `--cancel-event NAME` | Open an existing Windows event by name. Signalling it stops a run cooperatively. Ctrl+C or Ctrl+Break can also stop a run in an attached console. |

Use one stop condition at most. Numbers accept decimal or `0x` hex. The runner checks conditions at Macintosh instruction boundaries and PC CPU step boundaries, including x86 prefix steps and REP iterations. Physical memory diagnostics cover the Macintosh’s 128 KiB RAM or the PC’s conventional RAM and EGA aperture below `0xC0000`. Supply up to 1,024 input events in cycle order. They take effect at the first CPU boundary at or after the requested cycle.

The JSON includes `status`, `cycles`, and physical `pc`. Macintosh adds `vbl`. PC adds `cs`, `ip`, `halted`, and the last POST byte and exception vector when available. Optional `memory` contains the physical address and hexadecimal bytes. A bus fault reports its operation and address.

Mac-only `--mac-cpu-state 1` adds stopped D0-D7, A0-A7, PC and SR values
without stepping or reading the guest bus. A7 is the active stack pointer.
The default result is unchanged. See [register observations](docs/mac-cpu-state.md)
for the JSON layout, native call/interrupt checks and scope limits.

PC runs with scheduled scancodes also report `pc_keyboard_input` counts for
scheduled, accepted, rejected and pending bytes. Acceptance is into the model,
not evidence of guest consumption. See the [PC/AT guide](docs/pc-at.md) for
rejection and early-stop behavior.

`--convert-capture INPUT --output OUTPUT` converts a headless capture to GIF, MP4 or PNG using the FFmpeg bundled next to the executable. See [headless capture](docs/headless-capture.md) for bundle assembly and exact examples.

When video is requested, `video` in the result reports frame count, dimensions, sampling rate, actual last sampled cycle and whether a frame was omitted after the limit. The [capture guide](docs/headless-capture.md) explains timing and output size.

| Exit code | Result |
| --- | --- |
| 0 | `condition_met`, or `cycle_limit` / live `closed` / PC `halted` with no condition requested |
| 1 | `timeout`, `cancelled`, `machine_fault`, `input_overflow`, `input_error`, or an exit before a requested condition |
| 2 | `error` in the request, image handling, or diagnostics |

If a readiness condition has not matched when the cycle limit is reached, status is `readiness_not_reached` and exit code is 1. Timeout and cancellation retain their usual statuses. With `--input-after`, `input_schedule` reports whether readiness was observed, the boundary cycle when available, the number scheduled, and the number accepted. `mouse.steps` and `mouse.pending` report delivered and pending transitions. A stop condition is evaluated independently and may stop a run before all scheduled work is accepted. `--mouse-record` supports direct relative motion as well as window input, recording absolute accepted cycles for replay. Reproduce separately scheduled keyboard input during replay. Readiness reads and all inspections are side-effect-free.

When scancodes are scheduled, the result also reports the controller output
queue at stop independently of `--pc-witness`; see [PC/AT keyboard accounting](docs/pc-at.md).

The 286 witness begins with `BHPC286W1 STOP COUNT DROPPED` and a column header. Each row gives cycle, CS, IP, physical PC, kind, address, value and width. `step` marks a CPU step, including prefixes and REP iterations. Filtered `read`/`write` are byte bus accesses, `in`/`out` are port operations, `irq` is an acknowledged hardware interrupt vector, and `exception` is a change in the CPU's last-exception value. Repeated identical exceptions may therefore be omitted. Software interrupt instructions and firmware-shim memory operations are not separately traced. Accesses within a CPU step share the step's starting cycle and instruction location. A maximum of 65,536 events is retained and further events are counted in `DROPPED`. A cycle filter is recommended for long runs. This is emulator execution evidence, and does not establish physical-hardware timing.

## Live Mac mouse tests

For the exact local ClasSICK 0.97 fixture, automated pass criteria, and diagnostic addresses, see the [ClasSICK 0.97 Mac mouse runbook](docs/classick-097-mouse.md).

For the anchored ClasSICK candidate, run this from a terminal with `$rom` set to your local ROM and `$trace` set to a new file path.

```powershell
& "$build\boothush.exe" --machine mac128k --rom $rom --live 2 --mouse-dcd-invert 3 --cycles 440000000 --timeout-ms 60000 --mouse-record $trace --inspect 0xb00:1024
& "$build\boothush.exe" --machine mac128k --rom $rom --mouse-replay $trace --cycles 440000000 --timeout-ms 60000 --inspect 0xb00:1024
```

Activate the BootHush window and wait for the ROM desktop. The first click on the inactive viewport activates it. Move the guest pointer using relative motion and use the displayed guest pointer to aim. Entering the viewport anchors motion at the current host position. A held primary button captures movement through release, with coordinates clamped at the viewport edges. Let the pointer finish moving before closing if the trace should include the whole action. The JSON `mouse` fields report accepted `samples`, delivered `steps`, and `pending` transitions at stop.

The text trace starts with `BHMOUSE1 EDGE_CYCLES DCD_INVERT STOP_CYCLE VBL_GUARD`. Each following line stores `CYCLE DX DY BUTTON`, with `0` for unchanged, `1` for release, and `2` for press. Reuse the same ROM, media, and any scheduled `--key` events when replaying. The run limits still apply. `--inspect` can read the ClasSICK ROM's `MWP1` state, `MLC1` loss counters, `MMD1` omissions, and `MCM1` coalescing counter. `--mouse-phase` remains available for exact pin-level experiments.

`--window-mouse` runs the normal window message handler with an invisible owned window and labels its JSON result `input_source: injected_window_messages`. This is independent of the Windows primary-button setting and uses no global mouse injection. Direct `--mouse-motion` and `--mouse-button` events are scheduled emulator input. Automated interaction with a visible `--live` window uses Windows' actual input path. A person operating a physical mouse is a separate observation. The visible window remains an explicit opt-in. Start a controller with `CREATE_NO_WINDOW` to prevent a console window, too.

The live clock is paced to 7,833,600 guest cycles per second. For a longer bounded run use, for example, `--cycles 4000000000 --timeout-ms 600000`, which allows roughly 8.5 minutes before the cycle cap. Close the window or signal `--cancel-event` to stop earlier. The original 60-second timeout remains available by requesting `--timeout-ms 60000`.

The 8,192-cycle edge spacing and ±4,096-cycle VBL guard were chosen empirically for reliable delivery to the tested ClasSICK ROM. They are input pacing controls, with no physical mouse timing measurements behind them. A zero guard and shorter edge spacing are available to expose guest timing sensitivity. The model produces Gray-code phases 0, 1, 3, 2 and uses phase bit 0 as SCC DCD and bit 1 as VIA input. It latches the first enabled SCC DCD transition until acknowledgement, retains a pending change when the acknowledged latch differs from the live level, and samples the VIA phase on the guest's read. The CPU executes whole instructions, so events land at instruction boundaries and device witness cycles reflect emulator timing. This model has not been validated against physical Macintosh timing. A phase change during an SCC pending interval can therefore expose a guest race in this simulation, without establishing physical-hardware behavior or identifying the cause of every missed movement.

Recordings reproduce accepted input at guest cycle boundaries with the same ROM, media, settings and scheduled keys. `mouse.pending` reports transitions left at stop. Detailed witnesses also report SR interrupt-mask changes; `--mouse-witness-cpu 1` adds each instruction boundary within the selected range; see the [mask probe and limits](docs/mac-mouse-witness.md#cpu-interrupt-mask-observations). Witness headers report the number of rows dropped when the 65,536-row capacity is reached. Different ROMs, input settings, early timeouts or changed emulator builds may change results. Matching replay establishes reproduction within BootHush.

### Device trace

To see which signals the guest observed, replay the same input with a device trace over the cycle window of interest.

```powershell
& "$build\boothush.exe" --machine mac128k --rom $rom --mouse-replay $trace --cycles 440000000 --timeout-ms 10000 --mouse-witness $newWitness --mouse-witness-range 108000000:110000000
```

`BHMACMOUSE1 FIRST LAST STOP COUNT DROPPED` gives the requested range, actual stop, number of rows saved, and number dropped after the 65,536-row limit. The next line labels the columns. Rows show pin or button changes, SCC control accesses, mouse and interrupt-related VIA accesses, and CPU interrupt-level changes. `snapshot` records the initial state when the guest reaches the range. `scc_latched` and `scc_dcd` use bit 0 for X and bit 1 for Y, and `scc_pending` uses the SCC RR3 encoding. `address`, `reg`, and `value` describe the bus access on SCC/VIA rows. Device state is sampled after the event. Cycle labels use the emulator's instruction timing, with file order preserving event order within a cycle. The trace has no guest-visible side effects.

## Third-party code

[Moira](third_party/moira/UPSTREAM.md) provides the 68000 core under MIT. [MAME’s i286/i86 core](third_party/mame-i286/README.md) provides the 80286 CPU under BSD-3-Clause. Those directories retain the upstream notices and revision records. The [Macintosh floppy](docs/mac-floppy.md), [keyboard](docs/mac-keyboard.md) and [RTC](docs/mac-rtc.md) notes document their respective device references. BootHush source and test fixtures are kept separate from the ClasSICK OS and ROM repositories.
