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

For deterministic loader error testing, `--fail-floppy-read C:H:S` requires the `pc286` profile, floppy image and built-in firmware (no `--rom`). It matches the **starting** CHS of every otherwise valid floppy `INT 13h AH=02h` read, including retries, and returns carry set, `AH=20h` (controller failure), `AL=0` and the same last-status value. No bytes are copied on an injected failure. Geometry and buffer errors take precedence. It does not affect fixed disks, verify, direct floppy-controller I/O, custom firmware, or reads that merely span the selected sector. On every run with the option, JSON includes `floppy_read_failure` with the selected `cylinder`, `head`, `sector`, `bios_status` (32), and `injected` count; zero means the selection was never reached on a valid read. The bootstrap's own INT 13h read is included. The original floppy remains unchanged. Injection is disabled unless explicitly requested.

`--fail-hard-disk-read C:H:S` provides the same starting-CHS failure for a
valid built-in firmware `INT 13h AH=02h` hard-disk read. It requires `pc286`,
`--hard-disk` and no `--rom`; the selection must fit the model's 615:4:17
geometry. Each matching read returns carry set, `AH=20h`, `AL=0`, and records
the fixed-disk last status without copying data. Geometry and buffer errors
take precedence. It does not inject failures into floppy reads, disk writes,
verify calls, custom firmware or direct controller I/O. Reads that merely
span the selected CHS do not match. JSON `hard_disk_read_failure` records the
selection and injection count, including any matching bootstrap read. The
original disk image stays unchanged; injection is disabled by default. This is
a deterministic modeled failure, not a physical controller fault model.

`--fail-hard-disk-write C:H:S` applies the same starting-CHS selection and
geometry/firmware restrictions to valid `INT 13h AH=03h` fixed-disk writes.
It returns carry set, `AH=20h`, `AL=0`, records the disk status, and makes no
changes for that injected write. `hard_disk_write_failure` reports the
selection and count. It does not affect reads, verification, floppy, direct
controller I/O or custom firmware. Read and write injections can be selected
independently. A later error does not roll back earlier successful writes; a
requested `--disk-out` snapshot is saved when the overall run is successful,
including when an expected failure POST satisfies the stop condition.

The machine has RAM at `00000h`–`9ffffh`, EGA aperture in `a0000h`–`bffffh`, optional EGA ROM at `c0000h`, system ROM at `f0000h` and the reset ROM alias at `ff0000h` in the 24-bit space. `--scancode CYCLE:BYTE` schedules raw keyboard set-2 bytes at guest CPU boundaries, including `f0` followed by the key byte for release; it does not touch the host keyboard. The built-in keyboard shim handles a limited set of translated keys, not an entire BIOS keyboard implementation. See [EGA diagnostics](ega.md) for graphics and caller-supplied text-font limitations.

When at least one scancode is scheduled, the result JSON includes
`pc_keyboard_input` with `scheduled`, `accepted`, `rejected`, and `pending`
counts. Accepted means the keyboard model accepted the scheduled byte or
translation prefix; it does not prove the guest read or acted on it. The first
rejected byte stops the run as `machine_fault`; later events remain pending.
The stop condition is checked before input at a boundary, so an early stop may
leave pending events. No field is emitted if no scancodes were scheduled.
This accounting describes injection into the model, not physical input.

The `--hard-disk` input is loaded into a private memory copy. Guest fixed-disk writes affect that copy, not the input file. `--disk-out NEW_PATH` writes the copy only if the run succeeds and the output path does not exist. Supply that new file as `--hard-disk` on the next run to test persistence. Failure, timeout, cancellation or an unmet condition does not save the disk copy. A snapshot or a successful boot of an original fixture is not a ClasSICK installation. Keep input disks and generated outputs outside Git.

With a fixed emulator build, ROM, media, cycle-scheduled input and stop condition, guest output can be compared between runs; a wall-clock timeout, changed build or external host input can change the outcome. The bounded 286 witness records steps, filtered byte memory and port events, IRQ acknowledgements and changes to the last exception; it can drop rows after 65,536 events, and firmware-shim operations are not independently traced. See the [README witness contract](../README.md#local-process-api).

ClasSICK's generated fixed-disk diagnostic additionally exercised a bitwise
CRC-16 payload check and read-failure controls on the 286 model. Its full
window session schedules keyboard input after a separate modeled readiness
observation because CRC startup takes longer than the old sum. The exact
guest, images, emulator identity, tests and limitations are recorded in
`C:/Repos/ClasSICK-0.97/docs/development/mac-pc286-shared-parity-20261114.md`.
This integration result does not establish a physical BIOS or installation.
The same diagnostic now optionally checks the built-in model's returned
geometry before reading its payload; wrong expected geometry and unsupported
drive controls stop before loading. The paired session, source and model
limitations are recorded in the same ClasSICK evidence.

## Hardware references

- System board I/O, timers, interrupt routing, DMA, keyboard controller and CMOS
  layout. [IBM Personal Computer AT Technical Reference, March 1984](https://www.minuszerodegrees.net/manuals/IBM_5170_Technical_Reference_1502243_MAR84.pdf)
- Fixed-disk and diskette adapter ports and disk type parameters. [IBM PC/AT Fixed Disk and Diskette Drive Adapter](https://computermuseum.informatik.uni-stuttgart.de/ftp/mirrors/www.minuszerodegrees.net/oa/OA%20-%20IBM%20PC%20AT%20Fixed%20Disk%20and%20Diskette%20Drive%20Adapter.pdf), [IBM Personal Computer AT Technical Reference, September 1985](https://minuszerodegrees.net/manuals/IBM_5170_Technical_Reference_6280070_SEP85.pdf)
- PIC, PIT and RTC register behavior. [Intel 8259A](https://www.cs.umb.edu/cs341/Intel8259/I8259APIC.pdf), [Intel 8254](https://www.cs.cmu.edu/~410/doc/8254.pdf), [Motorola MC146818A](https://bg-electronics.de/datenblaetter/Schaltkreise/MC146818A.pdf)
