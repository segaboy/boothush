# Original diagnostic font in the paired preview

2026-11-16. `scripts/make_diagnostic_font.py` generates original 5x7 lettering
in a 256-glyph, 8x14 caller font for the existing `--ega-font` option. Lowercase
uses capitals; unsupported glyphs are boxes. No BIOS font, firmware extraction
or historical CP437 fidelity is claimed. GPL-3.0-or-later.

The generated 3,584-byte font is
`0d2224d4908bd4856e001b1e7aeb2409b92ebd785d5691d41db6253759ea0517`.
The source and output receipt are retained in
`C:/Repos/.task-temp/boothush-original-font-20261116-a`.
The existing active Mac/286 executable remains unchanged, SHA-256
`8860227d991a6c6d252815019c2458a1b0af3cae49306ce43e7fdaff5dd04e3b`.

Actual integration: 16 ClasSICK preview boot/installation cases used this
font, including cancellation, occupied targets, corruption, injected I/O
failures, retries, installation and installed boot. The canonical receipt is
`C:/Repos/ClasSICK-0.97/docs/development/paired-preview-readiness-20261116.md`;
the exact run is `C:/Repos/.task-temp/pc286-preview-qualification-20261116-b`.
Prompt and successful-install frames were visually checked. This fixes the
capture dependency on supplied text glyphs; it changes no firmware or CPU.

The same active tool produced both eight-state native parity runs, the
14-checkpoint edge matrix, and a 131-frame Mac Y4M recording converted with
the existing bundled FFmpeg. Those are scheduled emulator results. The font
and media do not close physical, GUI-host, human or release gates.
