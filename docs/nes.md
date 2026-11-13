# Experimental NES target

BootHush has a first headless Nintendo Entertainment System development target. It executes CPU instructions against an original caller-supplied NROM cartridge. It is not yet a game-playing NES emulator: there is no PPU, display, APU, audio, controller, external interrupts, DMA, save RAM, cartridge bank switching, or CPU/PPU timing. The CPU supports all 151 documented 6502 opcode encodings but has not passed a complete independent conformance suite. Do not treat these results as physical-hardware validation.

```powershell
& 'C:\Repos\boothush\out\windows\boothush.exe' --machine nes `
  --rom 'C:\path\to\original-test.nes' --steps 100000 --timeout-ms 5000 `
  --until-pc 0x800e --inspect 0x10:1
```

Use your own original or appropriately licensed cartridge. BootHush does not supply game ROMs or fetch them. The program address in the example is fixture-specific. `--steps` is an instruction bound from 1 to 1,000,000, **not CPU cycles**. `--timeout-ms` is required and checked at instruction boundaries. `--until-pc` is optional and stops before executing that address, including the reset address; an unreached condition returns `condition_not_met` and exit 1. Without it, `step_limit` is a successful bounded stop (exit 0). A fault or timeout exits 1; invalid input or an unsupported option exits 2. This experimental target does not yet support `--cycles`, `--until`, multiple inspections, `--frame`, video, scheduled input, cancellation events, or witnesses; those options are rejected, not silently ignored.

The result includes the cartridge SHA-256, instruction count, PC, accumulator, X, Y, SP and flags. One optional `--inspect ADDRESS:LENGTH` reads up to 4096 bytes of supported CPU address space without side effects; it cannot span an unmapped/device region. The emulator opens the cartridge read-only and never writes it. A CPU stop reports the unsupported opcode or bus access, instruction address and relevant bus address. Faulting instructions do not increment the instruction count; register changes are rolled back. This diagnostic does not reproduce cycle-level bus activity, open-bus behavior or reset timing.

## Supported cartridge and memory

The loader accepts a strict, exact-size **iNES 1.0 mapper 0** image with one or two 16 KiB PRG-ROM banks and zero or one 8 KiB CHR-ROM bank. A 16 KiB bank is mirrored into both CPU ROM windows; a 32 KiB image occupies both windows separately. CHR contents are validated for length but not used. The loader accepts the header's horizontal/vertical mirroring bit; nametable mirroring itself is not implemented. It rejects NES 2.0, other mappers, trainers, battery-backed RAM, four-screen memory, nonzero extended/reserved header fields, and trailing or truncated data. These are intentionally narrow restrictions, not claims that such cartridges are invalid NES media.

The reset vector is read from `FFFC`–`FFFD`. The deterministic initial model sets A, X, Y and the 2 KiB RAM to zero, SP to `FD`, and flags to `24`; real console power-on state is not guaranteed to match. CPU addresses `0000`–`1FFF` mirror that RAM every 2 KiB. The cartridge occupies `8000`–`FFFF`. Unimplemented address reads and writes, including PPU, APU, controllers, PRG RAM and writes to cartridge ROM, stop with an explicit fault. ROM-write faulting is a diagnostic restriction, not a model of physical cartridge bus conflicts.

The interpreter implements the 151 documented 6502 opcode encodings and addressing modes, including stack operations, BRK/RTI, and the indirect-JMP page-wrap behavior. Arithmetic remains binary even when the decimal flag is set, as on the NES CPU. Undocumented opcodes fault before being counted. BRK is an interrupt instruction, not a stop command. Device-triggered NMI and IRQ are not implemented. Instruction timing, dummy bus accesses, interrupt races, and cycle-accurate behavior are not modeled.

The tests generate their own cartridges in temporary directories and cover both NROM PRG layouts, reset vectors, mirrored RAM, instruction flow, unsupported devices and opcodes, bounded stops, malformed cartridges, and unchanged input. A separate CPU test checks all 151 documented opcode encodings are accepted and undocumented encodings rejected; exhaustively checks 262,144 ADC/SBC input and carry combinations with the decimal flag set; and tests representative addressing modes, stack/interrupt instructions, flags and page wrapping. Opcode acceptance is not exhaustive instruction conformance. These tests do not demonstrate graphics, controller operation, any commercial game, full CPU conformance, or physical timing.

## Hardware references

- [NESdev: iNES cartridge format](https://www.nesdev.org/wiki/INES)
- [NESdev: NROM cartridge mapping](https://www.nesdev.org/wiki/NROM)
- [NESdev: NES CPU and memory map](https://www.nesdev.org/wiki/CPU_ALL)
