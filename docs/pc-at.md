# IBM PC/AT boot profile

BootHush models one 6 MHz 80286, 640 KiB of conventional RAM, an enhanced-color
EGA adapter, one 1.2 MB diskette drive and one IBM type 2 fixed disk. The fixed
disk has 615 cylinders, four heads and 17 sectors per track. The diskette is
read-only. Fixed disk writes change the run's private copy. Save that copy with
`--disk-out` to carry an installation into a second run.

A supplied system ROM runs from the 80286 reset vector. It can call an optional
EGA ROM at `C0000h` and access the PC/AT device ports directly. The model routes
8259A interrupts from the PIT, keyboard, floppy, RTC and fixed disk. The floppy
controller reads media through 8237 DMA channel 2 and can transfer bytes through
its data port. The fixed-disk controller uses CHS and PIO, including word
transfers at `1F0h`. RTC and timers derive their state from guest cycles.

With a floppy or hard disk alone, BootHush's own firmware loads a boot sector and
provides BIOS video, equipment, conventional memory, CHS disk, keyboard and
clock calls. It supports the calls exercised by the boot tests. Supplied system
ROMs implement their own BIOS calls and use the emulated board.

The generated tests exercise reset, protected mode, POST markers, EGA option
ROM mapping, floppy DMA and IRQ6, hard-disk PIO, and a floppy-to-hard-disk
installation followed by a disk-only boot. A separate opt-in test runs the
official FreeDOS 1.4 boot floppy with the built-in firmware. Factory IBM BIOS
POST remains unverified. Analog disk timing, floppy writes, battery-backed
CMOS persistence, additional drives and expansion hardware are outside this
profile.

## Built-in firmware contract for disk development

These calls are the BootHush built-in firmware shim, not a claim of full IBM BIOS compatibility. They apply when the PC uses built-in firmware; a supplied system ROM provides its own services. The shim boots a signed sector (`55 aa`) from a supplied floppy first, otherwise a signed hard disk, at `0000:7c00` with drive `DL=00h` or `80h`. A missing boot signature does not launch that medium. The floppy is raw 1,228,800 bytes (80 cylinders, 2 heads, 15 512-byte sectors); the hard disk is raw 21,411,840 bytes (615 cylinders, 4 heads, 17 sectors).

| Interrupt | Implemented built-in calls |
| --- | --- |
| `INT 13h` | `AH=00` reset, `01` last status, `02` read, `03` write, `04` verify address/range, `08` geometry, `15` drive type. Only `DL=00h` and `80h` are recognized when the corresponding media exists. Floppy writes return status `03h` and do not alter media. Format (`05`) and unsupported calls return an error. |
| `INT 10h` | `AH=00` mode setting (modes `03h` and `10h`), `01` cursor shape, `02`/`03` cursor position, `06`/`07` text scroll, `08`/`09`/`0a` text character operations, `0e` teletype, `0f` query mode. Text operations are restricted to mode `03h`; invalid or unsupported requests report carry and `AH=86h`. |
| `INT 11h`, `12h` | Equipment word and conventional-memory size from the BIOS data area. |
| `INT 15h` | `AH=88h` reports zero extended memory; other functions return carry and `AH=86h`. |
| `INT 16h` | `AH=00h`/`10h` wait for and consume a key; `01h`/`11h` check without consuming; `02h`/`12h` query modeled shift state. Unsupported functions set carry. |
| `INT 1ah` | `AH=00h`/`01h` get/set BIOS ticks, `02h` read RTC time and `04h` read RTC date; other calls set carry. |

Disk `AH=02h`–`04h` accepts 1–127 sectors and validates CHS. Floppy transfers cannot cross a track; hard-disk transfers may span tracks within the disk. Read/write buffers must lie entirely in conventional RAM and may not cross a 64 KiB physical boundary. Invalid geometry/ranges report `AH=04h`, invalid/cross-boundary buffers `AH=09h`, and unsupported drives or commands `AH=01h`, with carry set. Verify checks the modeled address/range; it is not a physical surface test. Firmware-shim copies are not separately represented as byte memory events in the 286 witness.

The machine has RAM at `00000h`–`9ffffh`, EGA aperture in `a0000h`–`bffffh`, optional EGA ROM at `c0000h`, system ROM at `f0000h` and the reset ROM alias at `ff0000h` in the 24-bit space. `--scancode CYCLE:BYTE` schedules raw keyboard set-2 bytes at guest CPU boundaries, including `f0` followed by the key byte for release; it does not touch the host keyboard. The built-in keyboard shim handles a limited set of translated keys, not an entire BIOS keyboard implementation. See [EGA diagnostics](ega.md) for graphics and caller-supplied text-font limitations.

The `--hard-disk` input is loaded into a private memory copy. Guest fixed-disk writes affect that copy, not the input file. `--disk-out NEW_PATH` writes the copy only if the run succeeds and the output path does not exist. Supply that new file as `--hard-disk` on the next run to test persistence. Failure, timeout, cancellation or an unmet condition does not save the disk copy. A snapshot or a successful boot of an original fixture is not a ClasSICK installation. Keep input disks and generated outputs outside Git.

With a fixed emulator build, ROM, media, cycle-scheduled input and stop condition, guest output can be compared between runs; a wall-clock timeout, changed build or external host input can change the outcome. The bounded 286 witness records steps, filtered byte memory and port events, IRQ acknowledgements and changes to the last exception; it can drop rows after 65,536 events, and firmware-shim operations are not independently traced. See the [README witness contract](../README.md#local-process-api).

## Hardware references

- System board I/O, timers, interrupt routing, DMA, keyboard controller and CMOS
  layout. [IBM Personal Computer AT Technical Reference, March 1984](https://www.minuszerodegrees.net/manuals/IBM_5170_Technical_Reference_1502243_MAR84.pdf)
- Fixed-disk and diskette adapter ports and disk type parameters. [IBM PC/AT Fixed Disk and Diskette Drive Adapter](https://computermuseum.informatik.uni-stuttgart.de/ftp/mirrors/www.minuszerodegrees.net/oa/OA%20-%20IBM%20PC%20AT%20Fixed%20Disk%20and%20Diskette%20Drive%20Adapter.pdf), [IBM Personal Computer AT Technical Reference, September 1985](https://minuszerodegrees.net/manuals/IBM_5170_Technical_Reference_6280070_SEP85.pdf)
- PIC, PIT and RTC register behavior. [Intel 8259A](https://www.cs.umb.edu/cs341/Intel8259/I8259APIC.pdf), [Intel 8254](https://www.cs.cmu.edu/~410/doc/8254.pdf), [Motorola MC146818A](https://bg-electronics.de/datenblaetter/Schaltkreise/MC146818A.pdf)
