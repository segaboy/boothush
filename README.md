# BootHush

BootHush is a headless Windows 11 emulator for fast, repeatable ClasSICK development tests. Each invocation loads local images, runs an isolated guest, returns one JSON result, and exits. Input and capture operate on guest devices.

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

- Set `BOOTHUSH_CLASSICK_ROM` to the local 64 KiB startup fixture with SHA-256 `be506ad6e41d06ebcfe11678a4f6a64ba560920b16efa7fef884794f51690005` to verify the original ROM’s diagnostic words and framebuffer checksum.
- Set `BOOTHUSH_CLASSICK_WINDOW_ROM` and `BOOTHUSH_CLASSICK_WINDOW_ROM_SHA256` for the short-collect invert-3 ClasSICK ROM with the CS09 and MWP1 v2 diagnostics. This exercises a scheduled press, release and four quadrature transitions, then checks guest motion and loss counters.
- Set `BOOTHUSH_FREEDOS_FLOPPY` to `120m/x86BOOT.img` from the [official FreeDOS 1.4 Floppy Edition](https://www.freedos.org/download/). The test verifies the boot image SHA-256, selects a language with a scheduled Enter, and checks the installer prompt.

## Local process API

Run `boothush.exe` directly from an existing terminal. When a controller launches a child with redirected streams, use Windows `CREATE_NO_WINDOW`. Python’s `subprocess.run` accepts it as `creationflags=subprocess.CREATE_NO_WINDOW`.

```powershell
& "$build\boothush.exe" --machine mac128k --rom $rom --cycles 80000000 --timeout-ms 10000 --until 0xb08:4:1 --inspect 0xb00:28
& "$build\boothush.exe" --machine pc286 --floppy $installer --hard-disk $blankDisk --disk-out $newDisk --cycles 4000000 --timeout-ms 10000
```

Select one `--machine`. Macintosh requires a ROM and accepts a floppy. The PC accepts a system ROM with optional media, or uses built-in firmware to boot supplied media with floppy first. Both `--cycles` (1–1,000,000,000 emulated CPU cycles) and `--timeout-ms` (1–60,000 wall milliseconds for guest execution) are required. Cycle counts can cross the cap by the cost of one CPU step. Image loading and requested capture occur outside the guest execution timer. A process supervisor can also impose an end-to-end timeout.

| Option | Behavior |
| --- | --- |
| `--rom PATH` | Read a 65,536-byte system ROM. PC mode maps it at the low and high BIOS ranges used during reset. |
| `--floppy PATH` | Mount read-only media. Macintosh accepts a raw 409,600-byte image or 400 KiB Disk Copy 4.2. PC accepts a raw 1,228,800-byte image. PC built-in firmware checks the boot signature and loads the selected sector at `0000:7C00`. |
| `--hard-disk PATH` | PC only. Mount a raw 21,411,840-byte IBM type 2 disk (615 cylinders, 4 heads, 17 sectors). Guest writes go to the private in-memory copy. |
| `--disk-out PATH` | PC only. Save the resulting hard disk to a new raw image after a successful run. Use that output as `--hard-disk` to boot it on a subsequent run. |
| `--ega-rom PATH` | PC only, with a system ROM. Map a valid EGA option ROM at `0xC0000`. |
| `--until ADDRESS:WIDTH:VALUE` | Stop when 1, 2 or 4 physical memory bytes equal the value in guest byte order. |
| `--until-pc ADDRESS` | Stop at a 24-bit physical instruction address. |
| `--until-post BYTE` | PC only. Stop when the guest writes the value to I/O port `0x80`. |
| `--inspect ADDRESS:LENGTH` | Return up to 4,096 bytes as hex without triggering guest device reads. |
| `--frame PATH` | Write a new image at stop. Macintosh emits binary PBM and PC emits binary RGB PPM. Existing files are protected. |
| `--mouse-button CYCLE:STATE` | Macintosh only. Schedule button state `1` pressed or `0` released. |
| `--mouse-phase CYCLE:X:Y` | Macintosh only. Set raw two-bit quadrature phases. Bit 0 drives SCC DCD and bit 1 drives VIA PB4/PB5. Default phases are `2:2`. |
| `--key CYCLE:CODE:STATE` | Macintosh only. Queue an M0110 scan position `0x00`–`0x39`, with `1` pressed and `0` released. |
| `--scancode CYCLE:BYTE` | PC only. Queue one raw keyboard Set 2 byte. For a release, schedule `0xF0` and then the key byte. |
| `--cancel-event NAME` | Open an existing Windows event by name. Signalling it stops a run cooperatively. Ctrl+C or Ctrl+Break can also stop a run in an attached console. |

Use one stop condition at most. Numbers accept decimal or `0x` hex. The runner checks conditions at Macintosh instruction boundaries and PC CPU step boundaries, including x86 prefix steps and REP iterations. Physical memory diagnostics cover the Macintosh’s 128 KiB RAM or the PC’s conventional RAM and EGA aperture below `0xC0000`. Supply up to 1,024 input events in cycle order. They take effect at the first CPU boundary at or after the requested cycle.

The JSON includes `status`, `cycles`, and physical `pc`. Macintosh adds `vbl`. PC adds `cs`, `ip`, `halted`, and the last POST byte and exception vector when available. Optional `memory` contains the physical address and hexadecimal bytes. A bus fault reports its operation and address.

| Exit code | Result |
| --- | --- |
| 0 | `condition_met`, or `cycle_limit` / PC `halted` with no condition requested |
| 1 | `timeout`, `cancelled`, `machine_fault`, or a cycle limit / PC halt before a requested condition |
| 2 | `error` in the request, image handling, or diagnostics |

## Third-party code

[Moira](third_party/moira/UPSTREAM.md) provides the 68000 core under MIT. [MAME’s i286/i86 core](third_party/mame-i286/README.md) provides the 80286 CPU under BSD-3-Clause. Those directories retain the upstream notices and revision records. The [Macintosh floppy](docs/mac-floppy.md), [keyboard](docs/mac-keyboard.md) and [RTC](docs/mac-rtc.md) notes document their respective device references. BootHush source and test fixtures are kept separate from the ClasSICK OS and ROM repositories.
