# Experimental NES target

BootHush has an experimental headless Nintendo Entertainment System target for caller-supplied NROM and a limited mapper-2 board. It includes a CPU, approximate PPU and APU models, OAM DMA, scheduled controller input, and audio capture. It is **not a complete NES emulator**: cycle-accurate PPU/APU behavior, persistent save RAM, most mappers, and full CPU conformance are missing. Graphics, audio and timing have known approximations. Do not treat results as physical-hardware validation.

For embedding in another application, see the [NES C library](nes-api.md). It uses the same core but has its own in-process interface and execution bounds.

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --machine nes `
  --rom 'C:\path\to\original-test.nes' --steps 100000 --timeout-ms 5000 `
  --until-pc 0x800e --inspect 0x10:1
```

Use your own original or appropriately licensed cartridge. BootHush does not supply game ROMs or fetch them. The program address in the example is fixture-specific. `--steps` is an instruction bound from 1 to 100,000,000, **not CPU cycles**. `--timeout-ms` is required and checked at instruction boundaries. `--until-pc` is optional and stops before executing that address, including the reset address; an unreached condition returns `condition_not_met` and exit 1. Without a stop condition, `step_limit` is a successful bounded stop (exit 0), not proof of guest success. A fault or timeout exits 1; invalid input or an unsupported option exits 2. `--frame NEW_PATH` writes the most recently completed 256×240 RGB PPM image without overwriting an existing file, and fails if no frame has completed. `--audio NEW_PATH` captures a maximum of 30 seconds as 44.1 kHz mono 16-bit PCM WAV, also without overwrite; `audio_truncated` reports exceeding the limit. `--video NEW_PATH` samples completed frames as Y4M, with `--video-fps` (1–60, default 10), `--video-frames` (1–3600, default 300), and `--video-start` (modeled CPU cycle, default 0). The JSON reports saved frames, limit, truncation and last cycle. The first sample is delayed until a frame completes; audio and video are separate files and are not guaranteed to align exactly. The Mac/PC `--cycles`, `--until`, cancellation events, and Mac/PC witness formats are not supported by this target.

`--controller1 MASK` and `--controller2 MASK` set the initial controller states. `--controller-event STEP:MASK` changes player one and `--controller2-event STEP:MASK` changes player two immediately before an instruction boundary. Up to 256 events are accepted in total; each player's events must be in nondecreasing step order, but the two players' options can be grouped independently. Events at the same step are applied in command-line order before the instruction. Masks use bits 0-7 for A, B, Select, Start, Up, Down, Left, Right. These are emulated inputs and do not touch host devices. A stop condition is checked before events at that boundary; `controller_schedule` reports combined scheduled, accepted and pending counts and `controllers` reports per-port counts when player two is specified. The modeled controller upper read bits are a fixed `0x40` approximation, not general open-bus behavior.

For example, run a bounded two-player headless input sequence with fresh output paths:

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --machine nes `
  --rom 'C:\path\to\tilt_no_network_unrom_(E).nes' --region auto `
  --steps 3000000 --timeout-ms 30000 --controller1 0 --controller2 0 `
  --controller-event 1000000:1 --controller-event 1030000:0 `
  --controller2-event 1000000:2 --controller2-event 1050000:0 `
  --frame 'C:\path\to\new-frame.ppm' --audio 'C:\path\to\new-audio.wav'
```

The example exercises input delivery; it does not claim those steps navigate a particular build to a match. Compare ROM hash, timing selection, input sequence and emulator build when reproducing a run.

The result includes the cartridge SHA-256, instruction and modeled CPU-cycle counts, PPU frame count, PC, registers, APU-write and OAM-DMA counts, audio sample count, capture and truncation flags, and `dmc_used` when the DMC memory reader has fetched a byte. Repeat `--inspect ADDRESS:LENGTH` up to eight times for CPU RAM or mapped cartridge memory, up to 4096 bytes per range; a range cannot span an unmapped or device region. The first range remains in `memory`; when several are requested, `memory_regions` contains all ranges in order. `--inspect-ppu ADDRESS:LENGTH` reads up to 4096 bytes of PPU address space `$0000-$3FFF`, including CHR, nametables and palette aliases; `--inspect-oam OFFSET:LENGTH` reads up to 256 OAM bytes. These observations are side-effect-free and made at the same stopped instruction boundary; they are not a save state. The cartridge is opened read-only. A CPU stop reports the unsupported opcode or bus access, instruction address and relevant bus address. Faulting instructions do not increment the instruction count; register changes are rolled back. This diagnostic does not reproduce cycle-level bus activity, general open-bus behavior or reset timing.

## Cycle-aligned scenarios

For comparisons at a common modeled guest time, `--controller-cycle CYCLE:MASK` and `--controller2-cycle CYCLE:MASK` schedule the two ports at CPU-cycle boundaries instead of instruction numbers. Do not mix cycle and instruction event types in one run. Each port's events must be ordered, with at most 256 events in total; events for both ports at the same requested time are applied in command-line order. A cycle is in the range 0-4,000,000,000. These inputs are rounded forward to the first instruction boundary at or after their requested cycle; CPU instructions, DMA and interrupts can advance across a requested point. `cycle_schedule.events` reports each requested cycle or offset, whether it was applied, and its target and actual cycle, instruction count and completed PPU-frame count. Accepted means the emulator set the controller state, not that the guest read it.

`--input-after ADDRESS:WIDTH:VALUE` makes cycle input and cycle observations relative to the first boundary where the CPU RAM predicate matches. NES widths 1, 2 or 4 are interpreted **little-endian**; the complete range must stay within CPU RAM and its mirrors `$0000-$1FFF`. Unlike the Mac predicate, this value uses NES byte order. `readiness` reports whether and when it matched. With no input events, the same readiness anchor can be used for neutral observations. If the instruction bound is reached before readiness, the result is `readiness_not_reached`, exit 1. A timeout or machine fault keeps its own status.

Choose at most one stop condition: `--until-cycle CYCLE` stops at the first boundary at or beyond an absolute modeled CPU cycle; `--until-frame N` stops at the first boundary with at least N completed PPU frames; `--until-input-cycle OFFSET` stops at or beyond the requested number of cycles after readiness and requires `--input-after`; or use the existing `--until-pc`. `--steps` and `--timeout-ms` remain mandatory bounds. If the step bound is reached without the selected stop, status is `condition_not_met` and exit 1. `stop_condition` reports the requested point, actual cycle/frame and, where applicable, the absolute target cycle. The actual point may be later and different runs may round to different boundaries.

Repeat `--observe-cycle CYCLE:ADDRESS:LENGTH` up to 32 times to record side-effect-free CPU RAM or mapped PRG-ROM samples, 1-256 bytes each. A range must stay wholly in `$0000-$1FFF` or `$8000-$FFFF`. Samples are sorted by requested cycle, preserve command-line order on ties, and use readiness-relative offsets when `--input-after` is present; otherwise they are absolute. `cycle_observations` reports requested, completed and pending counts and each captured value with requested/target/actual cycles, instruction, frame and address. These are instruction-boundary snapshots, not an access trace. Pending observations have no sample. The feature does not capture multiple screenshots in one run.

At each boundary, modeled video sampling happens first, followed by readiness, due memory observations, stop-condition evaluation, time/step limits, due input events and the next instruction. Consequently, a sample at an input's cycle sees state before that input is applied, and an event coinciding with a stop remains pending. `--frame` and final `--inspect` refer to the stopped boundary. Set `--controller-timing 1` on a legacy instruction-based schedule to report its applied cycle/frame per event in `instruction_schedule`; this is useful for translating an existing scenario to cycle-based scheduling. It cannot be combined with cycle-based events and is not a guest-read trace.

For example, with a fixture that writes `1` to CPU RAM `$10` when ready, capture two observations and stop 100,000 modeled cycles later:

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --machine nes --rom 'C:\path\to\fixture.nes' `
  --steps 1000000 --timeout-ms 5000 --input-after 0x10:1:1 `
  --controller-cycle 0:1 --controller-cycle 30000:0 --controller2-cycle 0:2 `
  --observe-cycle 50000:0:32 --observe-cycle 100000:0:32 --until-input-cycle 100000
```

Use neutral inputs or no controller events and the same anchor and observation offsets for a baseline. Preserve the ROM hash, executable hash (`Get-FileHash`), region, command/schedule and JSON with each run; BootHush does not automatically hash its executable or assign scenario identities. The Mac's `--key` and `--input-after` already use modeled Mac CPU cycles and big-endian RAM values. NES PAL and Mac VBLs have different timing: do not equate frame counts across the machines without an explicit mapping or tolerance. Successful aligned scenarios are emulator evidence, not proof of equivalent gameplay or hardware fidelity.

### Guest controller witness

`--controller-witness NEW_PATH` records guest writes to `$4016` and reads from `$4016`/`$4017` to a new text file. It is opt-in, protects existing files, and retains at most 8,192 events; `controller_witness` in JSON reports retained and dropped events. Optional `--controller-witness-cycles FIRST:LAST` restricts recording to an inclusive range of **absolute modeled CPU cycles**, independent of a readiness anchor. A range requires a witness path. This makes it possible to distinguish scheduled controller state from bits actually returned to guest code.

The header is `BHNESCTRL1 FIRST LAST STOP COUNT DROPPED`, followed by columns `cycle step frame pc kind port value strobe`. `kind` is `strobe` for a write and `read` for a guest read. Port 0 denotes the shared strobe write, and ports 1 and 2 denote controller reads. Values are decimal: a read includes the model's fixed `0x40` upper bits, with the returned serial bit in bit 0; a strobe value is the written byte. `strobe` is the resulting latch state. Cycle, instruction step and PC are from the start of the modeled instruction; multiple accesses during an instruction share that timing, and this is not a bus-cycle trace. Recording is observational and does not emulate physical-controller timing or prove which game character moved. Witness output can contain detailed guest behavior, so preserve it locally alongside its ROM/build identity.

The optional hash-pinned Super Tilt Bro regression converts a recorded instruction-based navigation into cycle-based events, runs neutral, each port independently, and both ports to the same requested stop cycle, and repeats the combined scenario. It checks that the game reads both controller ports and receives set serial bits during the movement interval. Actual stop boundaries may differ slightly, and the test does not identify characters or prove the effects of individual bits. Set `BOOTHUSH_STB_UNROM` to the supplied unmodified beta ROM and run the `boothush` CTest suite to reproduce it. The exact game source revision remains unknown.

## Supported cartridge and memory

The loader accepts strict, exact-size **iNES 1.0 mapper 0** and **NES 2.0 mapper 0/submapper 0** images with one or two 16 KiB PRG-ROM banks and zero or one 8 KiB CHR-ROM bank. It also accepts **NES 2.0 mapper 2/submapper 1** (no bus conflicts) with 2, 4, 8, 16, or 32 PRG banks, one 8 KiB CHR-ROM or 8 KiB declared CHR RAM, and zero or 8 KiB declared volatile PRG RAM. `$8000-$BFFF` selects a 16 KiB PRG bank on a mapper write; `$C000-$FFFF` is the last bank. The declared volatile RAM, if present, is at `$6000-$7FFF`. NROM-128 mirrors its bank into both windows. iNES with no CHR ROM uses 8 KiB CHR RAM. NES 2.0 CHR RAM must be declared. The loader accepts the NES 2.0 battery flag without allocating persistent memory when NVRAM sizes are zero; nonzero NVRAM is rejected rather than silently discarded. Trainers, four-screen memory, other mappers/submappers, unsupported sizes and extra/truncated data are rejected. These restrictions are not claims that rejected images are invalid NES media.

The upstream Super Tilt Bro source revision `b132fd25add46f816e04be64c434386743b84b8b` declares the no-network UNROM build as mapper 2/submapper 1, 512 KiB PRG, 8 KiB CHR RAM, PAL, no PRG RAM, and a set battery flag. A generated cartridge with that header is tested. A separately obtained author-provided UNROM image, SHA-256 `5d045246f12d424c2c67ecda883c66d8d346f7b5396ed819ddb326f44dcb5aaa`, displays version 2.6 beta 5; its precise source revision is unverified. Headless PAL runs reached its title, options, character selection and an arena. A 13-million-instruction two-player scheduled sequence accepted all 16 input changes; compared with the same run without the final movement events, its frame, CPU RAM, PPU nametable and OAM differed. A repeated movement run matched the first run's JSON, frame, observations and cycle count. That comparison does not establish the effect of each controller separately or hardware fidelity. To rerun the optional integration test, point `BOOTHUSH_STB_UNROM` at that exact original file and run the test suite. It is not distributed in this repository. The current upstream checkout has not been built or tested, and Mapper 30 and Rainbow are different boards that remain unsupported.

The exact witnessed arena sequence for that particular ROM is:

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --machine nes `
  --rom 'C:\Repos\.task-temp\super-tilt-bro-author-unrom.nes' --steps 13000000 --timeout-ms 30000 `
  --controller-event 1000000:1 --controller-event 1030000:0 `
  --controller-event 1800000:1 --controller-event 1830000:0 `
  --controller-event 4000000:8 --controller-event 4050000:0 `
  --controller-event 6500000:1 --controller-event 6550000:0 `
  --controller-event 7500000:8 --controller-event 7550000:0 `
  --controller-event 10000000:128 --controller-event 11500000:0 `
  --controller2-event 6500000:1 --controller2-event 6550000:0 `
  --controller2-event 10000000:64 --controller2-event 11500000:0 `
  --inspect 0:2048 --inspect-ppu 0x2000:2048 --inspect-oam 0:256 `
  --frame 'C:\Repos\.task-temp\stb-arena-new.ppm'
```

Choose a fresh frame path for each run. To run the optional three-run regression as part of the suite:

```powershell
$env:BOOTHUSH_STB_UNROM = 'C:\Repos\.task-temp\super-tilt-bro-author-unrom.nes'
ctest --test-dir C:\Repos\boothush\out\windows --output-on-failure -R '^boothush$'
```

`--region auto|ntsc|pal` defaults to `auto`: iNES byte 9 and NES 2.0 byte 12 select NTSC or PAL; NES 2.0 multi-region defaults to NTSC and may be overridden. Dendy timing is rejected. JSON `cartridge` reports the header and selected region, whether an override was supplied, mapper, bank, sizes and battery flag. PAL uses 1,662,607 modeled CPU cycles/sec, a 16:5 PPU/CPU ratio with a preserved fractional accumulator, 312 scanlines without NTSC's odd-frame skip, and PAL APU noise/DMC periods and frame sequencing. NTSC uses 1,789,773 cycles/sec and 3:1 PPU/CPU timing. Video sampling and 44.1 kHz audio use the selected clock. Both models remain instruction-boundary approximations; PAL OAM refresh, analog video, exact APU write/reset timing and real-hardware conformance have not been established.

The reset vector is read from `FFFC`-`FFFD`. The deterministic initial model sets A, X, Y and the 2 KiB RAM to zero, SP to `FD`, and flags to `24`; real console power-on state can differ. CPU RAM is mirrored through `1FFF`; PPU registers through `3FFF`; the cartridge occupies `8000`-`FFFF`. OAM DMA copies from supported CPU RAM or ROM and adds a modeled 513/514-cycle stall. The approximate APU implements pulse, triangle and noise synthesis, length/envelope/sweep/linear counters, frame IRQ, status reads, DMC direct DAC and sample fetch/loop/IRQ, and a filtered nonlinear mix. DMC fetches are serviced at instruction boundaries with a fixed four-cycle charge; OAM/DMC bus contention and exact DMA alignment are not modeled. Frame-sequencer reset delay, mixer/filter response, timer edge cases and bus-cycle accuracy are approximate. Other unsupported bus accesses fault. NROM ROM-write faulting is a diagnostic restriction, not a model of physical cartridge bus conflicts.

The interpreter implements the 151 documented 6502 opcode encodings and addressing modes, including stack operations, BRK/RTI, and the indirect-JMP page-wrap behavior. Arithmetic remains binary even when the decimal flag is set. Undocumented opcodes fault before being counted. BRK is an interrupt instruction, not a stop command. CPU instruction cycle counts and branch/page-cross penalties drive the selected PPU timing, including the NTSC odd-frame skip when rendering is enabled. NMI and APU IRQs are delivered at instruction boundaries. Dummy bus accesses, PPU/CPU bus-cycle races and PPU startup write suppression are not modeled.

The PPU models register accesses, CHR and nametable mapping, palette aliases, buffered reads, vblank, and simplified sprite-zero hits. Its renderer approximates background, scrolling, up to eight sprites per scanline, priority, and a fixed RGB palette. It does not implement the original fetch pipeline, precise mid-scanline scroll changes, sprite overflow quirks, color emphasis, or composite output. Visible HUD/scrolling artifacts are known. A successful image is not evidence of a pixel-accurate or complete game.

Synthetic tests cover both NROM PRG layouts, NES 2.0 header validation, mirrored RAM, instruction flow, unsupported devices and opcodes, bounded stops, no-overwrite still/audio/video captures, controller latching, PPU mapping/buffering/NMI, simple rendering, and basic APU waveform, frame IRQ, DMC fetch, loop, address wrapping and IRQ. A separate CPU test checks all 151 documented opcode encodings are accepted and undocumented encodings rejected; exhaustively checks 262,144 ADC/SBC input and carry combinations; and tests representative addressing, stack/interrupt instructions, flags and page wrapping. Opcode acceptance is not exhaustive instruction conformance.

The optional `BOOTHUSH_NES_ROM` integration test uses the user's independently supplied Super Mario Bros. image with SHA-256 `0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea`. A title-screen run and a Start press produce different frames; a repeated Start run matches its full frame and JSON. A longer controlled comparison checks that right input changes RAM and the captured frame. The input is not redistributed. In observed runs the title screen and World 1-1 transition were recognizable, with movement, jumping and scrolling after scheduled inputs. A five-million-instruction run produced 87 sampled video frames and 379,783 WAV samples, including nonzero samples; a 25-million-instruction run reached its requested bound, accepted all eight events, and reported the 30-second audio cap. No listening or hardware-reference comparison was performed. These observations do not establish audio accuracy, a full playthrough, game compatibility, or hardware accuracy.

## Hardware references

- [NESdev: iNES cartridge format](https://www.nesdev.org/wiki/INES)
- [NESdev: NROM cartridge mapping](https://www.nesdev.org/wiki/NROM)
- [NESdev: NES CPU and memory map](https://www.nesdev.org/wiki/CPU_ALL)
- [NESdev: PPU registers](https://www.nesdev.org/wiki/PPU_registers), [memory map](https://www.nesdev.org/wiki/PPU_memory_map), and [frame timing](https://www.nesdev.org/wiki/PPU_frame_timing)
- [NESdev: APU registers](https://www.nesdev.org/wiki/APU_registers) and [mixer](https://www.nesdev.org/wiki/APU_Mixer)
- [NESdev: mapper 2 submappers](https://www.nesdev.org/wiki/NES_2.0_submappers), [PAL timing](https://www.nesdev.org/wiki/Cycle_reference_chart), [noise](https://www.nesdev.org/wiki/APU_Noise) and [DMC](https://www.nesdev.org/wiki/APU_DMC)
