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

## Hardware references

- System board I/O, timers, interrupt routing, DMA, keyboard controller and CMOS
  layout. [IBM Personal Computer AT Technical Reference, March 1984](https://www.minuszerodegrees.net/manuals/IBM_5170_Technical_Reference_1502243_MAR84.pdf)
- Fixed-disk and diskette adapter ports and disk type parameters. [IBM PC/AT Fixed Disk and Diskette Drive Adapter](https://computermuseum.informatik.uni-stuttgart.de/ftp/mirrors/www.minuszerodegrees.net/oa/OA%20-%20IBM%20PC%20AT%20Fixed%20Disk%20and%20Diskette%20Drive%20Adapter.pdf), [IBM Personal Computer AT Technical Reference, September 1985](https://minuszerodegrees.net/manuals/IBM_5170_Technical_Reference_6280070_SEP85.pdf)
- PIC, PIT and RTC register behavior. [Intel 8259A](https://www.cs.umb.edu/cs341/Intel8259/I8259APIC.pdf), [Intel 8254](https://www.cs.cmu.edu/~410/doc/8254.pdf), [Motorola MC146818A](https://bg-electronics.de/datenblaetter/Schaltkreise/MC146818A.pdf)
