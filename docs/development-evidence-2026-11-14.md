# Development evidence: 2026-11-14

The checkout started at `cf3c177`. The release executable used for the final regression has SHA-256 `1f306c427d1a8c42b9d547b2b25a424e4b4e6a389a52ae5861d030e0fe43bc25`. Source changes and exact CLI formats are described in [NES](nes.md), [keyboard](mac-keyboard.md), [mouse](mac-mouse-witness.md), [x64](classick-x64.md), and [scenario comparison](scenario-comparison.md). This document records modeled observations; none is physical-hardware validation.

## Tests

Built with the CMake, Ninja and LLVM-MinGW tools already configured in `out/windows`, and ran:

```powershell
cmake --build C:\Repos\boothush\out\windows
ctest --test-dir C:\Repos\boothush\out\windows --output-on-failure --parallel 2
```

All 16 CTest targets passed. The Python target ran 137 tests, with three skips for unsupplied ClasSICK startup/window fixtures and the official FreeDOS fixture. The run supplied these optional environment variables:

```powershell
$env:BOOTHUSH_NES_ROM='C:\Repos\Super Mario Bros. (World).nes'
$env:BOOTHUSH_STB_UNROM='C:\Repos\.task-temp\super-tilt-bro-author-unrom.nes'
$env:BOOTHUSH_STB_MAC_PROBE='C:\Repos\Not-Super-Tilt-Bro\out\controls-v1.rom'
$env:BOOTHUSH_CLASSICK_LIVE_ROM='C:\ClasSICK\mac-candidate-anchored-20261112-a\classick-live.rom'
$env:BOOTHUSH_CLASSICK_KEYBOARD_ROM='C:\ClasSICK\mac-keyboard-20261113-b\Os-a\classick-loaded-app.rom'
$env:BOOTHUSH_CLASSICK_X64_DISK='C:\ClasSICK\pc-fast-drag-20261112-a\window-drawing.img'
$env:BOOTHUSH_CLASSICK_X64_EFI='C:\ClasSICK\pc-fast-drag-20261112-a\BOOTX64.EFI'
$env:BOOTHUSH_TEST_FFMPEG='C:\Repos\boothush\out\windows\ffmpeg\ffmpeg.exe'
```

The Python binary selected by this CMake build was `C:\ClasSICK\bootstrap-clean-source-verified\.tools\llvm-mingw\llvm-mingw-20260908-ucrt-x86_64\python\bin\python.exe`. With that Python and the final BootHush executable, the following port-project commands also passed:

```powershell
python scripts\test_mac_controls.py --boothush C:\Repos\boothush\out\windows\boothush.exe
python scripts\test_mac_run_transition.py --boothush C:\Repos\boothush\out\windows\boothush.exe
```

The controls suite passed eight checks and exact replay. The running-entry suite passed six scenarios and exact replay using ROM SHA-256 `3c0232abce9c096aef41065bfa9da31f92ca8832b36aaa9afc6d1dca1560edc3`. This is a narrow source-derived state transition with placeholder display, not a playable game.

The NES witness fixture verifies filtering, bank switches, RAM reads/writes and old values, overflow, deterministic output, and hash-bound symbol rejection. A separate startup run of the supplied beta SHA-256 `5d045246f12d424c2c67ecda883c66d8d346f7b5396ed819ddb326f44dcb5aaa`, with cycles `0:50000`, produced 14,424 witness events with zero drops using the previous test executable SHA-256 `617c749601dd2866fcfe5a0b1fce81ae7984e150e62d5282fa87b4a8d22e6062`; the additional final build change was CLI help text and formatting. The beta source revision is unknown, so no source symbols were assigned.

The optional keyboard test uses the ClasSICK candidate SHA-256 `b3a50ad806b589fdc02a4de579e6d72700c9107fb273d9ca73026320bebed807`. Fault outcomes and the adjacent-cycle modeled timeout boundary are detailed in the keyboard guide. The mouse guide records the separate nine paired traced/untraced cases and four-direction sweep. The software x64 probe uses disk SHA-256 `80c279a69264cb07f2042bf14503c0687fa32e71057ee1196fc8174afbeeae4f` and EFI SHA-256 `df284097539ed6b7c90700cb27589900b20d2e4c7574b7637ba5f387abba9fe9`; it executes 30 instructions in an artificial invalid UEFI entry context. `--x64-capability` returned WHPX error `0x80070005`, with no guest execution.

## Remaining work

The comparison helper needs real project-authored action mappings, matched source builds, image/audio comparisons, and a source-to-target game scenario; its present test compares only original synthetic markers. The NES execution witness is CLI-only, is instruction-boundary rather than bus-cycle accurate, and does not trace PPU or DMA fetches. Macintosh sound remains unimplemented. Keyboard fault recovery has not been established in the supplied guest after its timeout latch. The mouse failure is reproduced but not attributed or fixed. A complete x64 CPU, UEFI environment, timers, graphics and input are still required to boot the actual disk. The port still needs usable 68000 build tools, full game logic, independently verified hardware behavior, and real-hardware testing.
