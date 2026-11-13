# Experimental NES target

BootHush has a first headless Nintendo Entertainment System development target. It executes a **limited CPU-only subset** against an original caller-supplied NROM cartridge. It is not yet a game-playing NES emulator: there is no PPU, display, APU, audio, controller, interrupts, DMA, save RAM, cartridge bank switching, timing, or complete CPU. Do not treat these results as physical-hardware validation.

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

The supported official opcodes are LDA immediate/zero-page/absolute; LDX and LDY immediate; STA zero-page/absolute; STX and STY zero-page/absolute; INX, DEX, INY, DEY, TAX, TAY, TXA, TYA; ADC and SBC immediate; CMP, CPX and CPY immediate; CLC, SEC, CLI, SEI, CLD, SED; JMP absolute; BEQ, BNE, BCC, BCS, BMI, BPL, BVC, BVS; and NOP. Arithmetic remains binary even when the decimal flag is set, as on the NES CPU. All other opcodes fault before being counted; BRK is not implemented as a stop instruction.

The tests generate their own cartridges in temporary directories and cover both NROM PRG layouts, reset vectors, mirrored RAM, instruction flow, binary arithmetic, unsupported devices and opcodes, bounded stops, malformed cartridges, and unchanged input. They do not demonstrate graphics, controller operation, any commercial game, full CPU conformance, or physical timing.

## Hardware references

- [NESdev: iNES cartridge format](https://www.nesdev.org/wiki/INES)
- [NESdev: NROM cartridge mapping](https://www.nesdev.org/wiki/NROM)
- [NESdev: NES CPU and memory map](https://www.nesdev.org/wiki/CPU_ALL)
