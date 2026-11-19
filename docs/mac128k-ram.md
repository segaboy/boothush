# Original Macintosh 128K RAM address decoding

The default `--machine mac128k` has 131,072 bytes of physical RAM. After
the VIA releases the startup overlay, guest addresses from `$000000` to
`$3FFFFF` select RAM. Only the low 17 address bits select a byte. For
example, `$01FFF0`, `$03FFF0` and `$3FFFF0` all address the same word.
A store through any of these addresses becomes visible through the others.

| Guest state | Modeled RAM addresses, inclusive | Example |
| --- | --- | --- |
| VIA startup overlay active | `$600000`–`$67FFFF` | `$67A700` addresses the main screen at physical offset `$1A700`. Reads at zero fetch the reset vectors from the low ROM image. |
| Overlay released | `$000000`–`$3FFFFF` | A longword starting at `$03FFFE` spans physical offsets `$1FFFE` and `$00000`. At `$3FFFFE` the second word is fetched from fixed ROM at `$400000`. |

Apple describes the repeating RAM block on printed page III-17, the VIA
overlay and reset sequence on III-42, and the shared 128K/512K screen and
sound addresses on III-45. Page III-45 also gives the overlay screen address
`$67A700`. The startup RAM row above covers the first 512 KiB beginning at
Apple's documented relocation base. The extent of startup aliases above
`$67FFFF` and additional ROM and device mirrors are outside this model's
qualification. [Apple Computer, *Inside Macintosh*, Volume III, “The Macintosh Hardware” (1985)](https://puyoman.net/documentation/Inside_Macintosh.pdf)

`--inspect` reads physical RAM offsets `$00000`–`$1FFFF`. The stopped CPU
PC and fault address report guest addresses. The detailed mouse RAM-write
witness records both the bus address and backing RAM offset. The low-RAM
step filter matches logical instruction starts in its selected low address
range. A jump to a higher RAM alias retains that higher PC.

## Qualification on 2026-11-19

Six original instruction fixtures exercise reads and writes through all
32 normal RAM windows, long accesses across a bank and into fixed ROM,
reset vectors and four high startup RAM windows, fixed ROM write protection,
logical instruction fetch, and the main screen, sound and disk speed buffer
addresses. The latter checks a rendered pixel and the physical buffer bytes.
The detailed mouse-witness regression checks separate logical and physical
addresses through both the startup and normal maps. The tests generate their
ROMs locally.

Two independent active-profile builds produced executable SHA-256
`a00fb8b507d79b0f0b88026c2e9cd613a6a7bd78c3aae10858bb5735e4f6db98`.
Both rebuilt the Mac, 286, NES, CLI and active dependencies and linked the
previously qualified x64 media archive, SHA-256
`1720431862157caa3a44c2cbd47fb4c6f64b75d5b3b22ccf70d0dd9b1b322ce6`.
The local build directory is `C:/Repos/.task-temp/boothush-ram-wrap-20261119-a`.
Its `report.json` records the input hashes and commands for both binaries,
`a/boothush-active.exe` and `b/boothush-active.exe`. The active build
procedure and retained archive are described in the
[build guide](active-profile-build-20261115.md).

The 21 headless test modules in `qualification.log` ran 189 cases. Of these,
174 passed and 15 skipped optional fixtures or opt-in runs. They include
Mac CPU and peripheral regressions, 286 firmware and storage controls, NES,
frame capture and cross-profile scenario comparisons.

To run the RAM fixtures alone, set `BOOTHUSH_EXE` to the qualified binary
and run `python -B -m unittest discover -v -s tests -p test_mac_ram_alias.py`
from the repository root.
