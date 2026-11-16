# IBM EGA diagnostics

For original diagnostic text, `python scripts/make_diagnostic_font.py --output
C:/path/to/new-font-directory` produces `diagnostic-8x14.bin` and a hash/rights
receipt. Pass that binary to `--ega-font`. Its independently drawn 5x7
lettering is doubled vertically; lowercase uses capitals and unsupported
characters use a box. It is not a firmware font, CP437 substitute or evidence
of a historical adapter's glyphs. This makes installer/error captures readable
without importing protected firmware assets. The default behavior is unchanged.
The [actual preview integration receipt](original-font-preview-20261116.md)
records the pinned font, installer observations and capture limits.

The original [286 paired-target diagnostic](pc286-pair-probe-20261114.md) checks all pixels of an unscaled 512x342 canvas at (64,4), including cursor restoration after keyboard-driven motion. It exercises this model and its built-in firmware, not historical adapter fidelity or a ClasSICK release.

BootHush models a single IBM EGA adapter with 256 KiB of planar video RAM and an enhanced color monitor. The graphics profile starts at 640×350 with 16 logical colors selected from the 64-color EGA palette. This lets a diagnostic ROM draw pixels immediately. A firmware shim can also select mode `10h` or mode `03h` with optional preservation of video RAM.

- The sequencer controls plane writes and odd/even CPU addressing. The graphics controller handles memory aperture selection, read-map selection, color comparison, EGA write modes 0–2, read latches, set/reset, rotation, raster operations, and bit masking.
- The attribute controller accepts palette and plane-enable writes. Reading the active status port resets its address/data flip-flop. Status bits report approximate horizontal blanking and vertical retrace from guest CPU cycles.
- Graphics capture reads the planar buffers without touching the guest's latches. The 640×350 snapshot uses the CRTC start address and row offset and reports top-to-bottom RGB. Mode `03h` stores character and attribute bytes at `B8000h` and captures black.

With `--ega-font PATH`, text-mode captures render an 80×25 grid of 8×14 glyphs into 640×350 RGB. The caller supplies exactly 3,584 bytes, 256 consecutive glyphs with 14 rows each and bit 7 leftmost. Character and attribute bytes use planes 0 and 1. The foreground uses the low attribute nibble, background uses bits 4–6, and bit 7 does not blink. The CRTC start address and enabled cursor shape/address are rendered; cursor blinking, alternate character sets, scrolling geometry, and other text modes are outside this profile. Without a supplied font, text capture remains black. Graphics rendering does not use the supplied font.

The device covers the fixed planar test profile. CGA-compatible shift modes, panning and split screens, programmable scan timing, vertical interrupts, the light pen, and the feature connector are outside this implementation. Undefined write mode 3 leaves video RAM untouched.

Register behavior is based on the August 1984 IBM *Enhanced Graphics Adapter* technical reference, particularly the external, attribute, sequencer, CRTC, and graphics-controller register sections. The switch setting represents the enhanced color monitor with the EGA as the primary adapter. [IBM EGA technical reference](https://minuszerodegrees.net/oa/OA%20-%20IBM%20Enhanced%20Graphics%20Adapter.pdf) · [EGA switch settings](https://book.martypc.net/appendices/video/ega-switches)
