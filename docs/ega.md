# IBM EGA diagnostics

BootHush models a single IBM EGA adapter with 256 KiB of planar video RAM and an enhanced color monitor. The graphics profile starts at 640×350 with 16 logical colors selected from the 64-color EGA palette. This lets a diagnostic ROM draw pixels immediately. A firmware shim can also select mode `10h` or mode `03h` with optional preservation of video RAM.

- The sequencer controls plane writes and odd/even CPU addressing. The graphics controller handles memory aperture selection, read-map selection, color comparison, EGA write modes 0–2, read latches, set/reset, rotation, raster operations, and bit masking.
- The attribute controller accepts palette and plane-enable writes. Reading the active status port resets its address/data flip-flop. Status bits report approximate horizontal blanking and vertical retrace from guest CPU cycles.
- Graphics capture reads the planar buffers without touching the guest's latches. The 640×350 snapshot uses the CRTC start address and row offset and reports top-to-bottom RGB. Mode `03h` stores character and attribute bytes at `B8000h` and captures black.

The device covers the fixed planar test profile. The EGA option ROM, text rasterization and fonts, CGA-compatible shift modes, panning and split screens, programmable scan timing, vertical interrupts, the light pen, and the feature connector are outside this implementation. Undefined write mode 3 leaves video RAM untouched.

Register behavior is based on the August 1984 IBM *Enhanced Graphics Adapter* technical reference, particularly the external, attribute, sequencer, CRTC, and graphics-controller register sections. The switch setting represents the enhanced color monitor with the EGA as the primary adapter. [IBM EGA technical reference](https://minuszerodegrees.net/oa/OA%20-%20IBM%20Enhanced%20Graphics%20Adapter.pdf) · [EGA switch settings](https://book.martypc.net/appendices/video/ega-switches)
