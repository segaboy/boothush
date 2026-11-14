# Original 286 keyboard-controlled frame experiment — 2026-11-14

The Mac/286 paired milestone needs repeatable input and redraw testing. This original, non-standalone-release diagnostic exercises the existing BootHush PC/AT BIOS keyboard and EGA model with a geometric 64x32 rectangular frame on a centered 512x342 white canvas. A space **make** toggles the experiment's drag state; a right-arrow make while enabled moves the frame eight pixels. A second space make releases it; a later right make is ignored. This is a deliberately simple test interface, not an observed or implemented ClasSICK window, hit test, real button hold, physical keyboard, historical PC or game behavior.

The original 1.2 MiB floppy SHA-256 was `b7771e9fdd4124594c9af6c8710beffa87205334dd3bbc3a08036075a3b0838f`; BootHush executable SHA-256 was `1f306c427d1a8c42b9d547b2b25a424e4b4e6a389a52ae5861d030e0fe43bc25`. The test source `tests/test_pc286_pair_canvas.py` SHA-256 was `9bee0769258d5803ab82a9e39d094b7a06ba4bfba601e76a6ed1222e07a2397f`. No Apple code, existing emulator implementation or external ROM/media is included in the fixture. The original input image remained unchanged.

Four separate cold boots scheduled the same make/break sequence and stopped on a guest POST marker. The test checked guest RAM at `0x600` and **all 640x350 RGB pixels**, including the former frame position and all margins, against an independent geometric oracle.

| Guest stop | Cycle | Frame x | Drag state | Moves | Frame SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| Press | 2,000,039 | 128 | 1 | 0 | `5f09f9565f3aefe69bae2c96de340193877027c74ba6f45f05220c2cd1e79117` |
| Move | 3,002,459 | 136 | 1 | 1 | `00d3bf5ea1229a1443fa5b30b6d23d05ed5a075dd7c1d19951a9a78ccb55f880` |
| Release | 4,000,034 | 136 | 0 | 1 | `00d3bf5ea1229a1443fa5b30b6d23d05ed5a075dd7c1d19951a9a78ccb55f880` |
| Post-release key | 5,000,034 | 136 | 0 | 1 | `00d3bf5ea1229a1443fa5b30b6d23d05ed5a075dd7c1d19951a9a78ccb55f880` |

The source test's four scenarios passed. Captures and a machine-readable manifest are retained outside Git under `out/mac-286-keyboard-drag-20261114`. Reproduce with a new output directory:

```powershell
$env:BOOTHUSH_EXE = 'C:\Repos\boothush\out\windows\boothush.exe'
$env:BOOTHUSH_TEST_TMP = 'C:\Repos\boothush\out\windows'
python -B -m unittest -v tests/test_pc286_pair_canvas.py
python -B scripts/capture_pc286_drag_probe.py --boothush out/windows/boothush.exe --output out/NEW-DRAG-CAPTURE
```

The four runs are repeatable modeled device input, not a live single-session GUI, E3/E4 evidence, a human test, a source-to-target parity proof or a release pass. The test exercises no OS installation, window manager, content click, title hit, keyboard rollover, serial mouse or physical hardware. Adoption of any finding into ClasSICK remains subject to its independent source-eligibility and role controls.

After this recording the test file was extended with a separate bitmap comparison; the hash above identifies the exact earlier source snapshot used for these four captures.

The moved PPM was also losslessly converted using BootHush's bundled converter to `out/mac-286-keyboard-drag-20261114/moved.png`, SHA-256 `4f2557d6099d0301822714b8697e3699019dc809a544c59a7fd324577f135f90`. It is original diagnostic artwork and not a ClasSICK window screenshot.
