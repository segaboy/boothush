# Experimental x64 EFI memory bridge — 2026-11-14

## Question and source

ClasSICK's native x64 loader needs a memory-map/key and page-allocation path before it can leave boot services. Can BootHush represent and serialize a limited test map with image ranges and reject stale exit keys? This slice is an original host-side model; it does not call a guest and cannot boot ClasSICK.

Behavior was independently implemented using the published [UEFI Specification 2.10, section 7, Boot Services](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html) and the project's existing `X64BootServices` model and tests. The published interface describes AllocatePages, GetMemoryMap, memory descriptors and ExitBootServices; the bounded adapter and its restrictions are BootHush design choices. No firmware or other emulator implementation was used. The receiving ClasSICK project should treat this as a tool test result, not an eligible implementation input or evidence of physical PC behavior.

## Changes and verification

Against BootHush base commit `f41ddc55beddb4acf11643fc3263372a469c0d6d`, working-tree changes add synthetic fixed image reservations, protect them from allocation/free, and translate selected service operations into a caller-owned byte buffer. Tests check little-endian descriptor fields, size negotiation, bounds, overlapping output refusal, allocation limits, image range exclusion, a failed free, key invalidation, incorrect handles, successful exit, and post-exit refusal. The EFI adapter is a standalone library test target and is **not linked to the CLI executable**. Calls are host calls, not EFIAPI or CPU execution. It implements only AllocateMaxAddress/LoaderData and exact FreePages; zero descriptor attributes and the contiguous memory model are approximations.

On the locally configured Windows LLVM-MinGW build:

```powershell
cmake --build out/windows --parallel 2
ctest --test-dir out/windows -R 'x64_(efi_memory|boot_services|pe_loader|soft_probe)' --output-on-failure
ctest --test-dir out/windows --output-on-failure --parallel 2
```

The four focused targets passed. With `BOOTHUSH_CLASSICK_X64_DISK` set to the original disk SHA-256 `80c279a69264cb07f2042bf14503c0687fa32e71057ee1196fc8174afbeeae4f`, the test also extracted EFI SHA-256 `df284097539ed6b7c90700cb27589900b20d2e4c7574b7637ba5f387abba9fe9`, mapped it at `0x10000000` using the existing PE loader, and reserved all 54 mapped pages as LoaderCode in a synthetic model. It then checked the descriptor and an allocation immediately below the image. Treating the entire image as code is a modeling choice; section-specific classifications, actual guest RAM and image execution remain untested. The complete suite passed 17/17 CTest targets, including the optional original ClasSICK Mac live and keyboard fixtures and x64 media inputs. Its Python target ran 137 tests with 10 skips for separately unsupplied fixtures: two older ClasSICK startup/window fixtures, FreeDOS, the port controls probe, two Super Tilt Bro NES fixtures, three other NES fixtures and FFmpeg. No skipped fixture is claimed as tested. The unchanged CLI SHA-256 is `1f306c427d1a8c42b9d547b2b25a424e4b4e6a389a52ae5861d030e0fe43bc25`; it is not evidence that the new host-only bridge is integrated.

## Port regression and limits

The separate Not Super Tilt Bro checkout `3db162d178f2706c324c19d7e52cd7597a6fe189` ran `scripts/test_mac_movement.py` on that CLI and passed its modeled neutral, movement, replay, boundary and keyboard-fault scenarios. The generated ROM SHA-256 was `bf7a065eb4d9a8141b7199ebe41eeb6d7abfef74f52d1d5e08948f5470294dd9`; outputs remain in its ignored `out/coordination-movement-20261114` directory. This reproduces the port's existing modeled experiment, not a physical test, complete game, or new historical finding. Its source-derived game implementation is not a ClasSICK input.

This bridge does not classify actual image sections, describe firmware, GOP or a complete system memory map, map allocated pages into guest RAM, install callable service tables, supply x64 execution, or run the real candidate. The `pc286` machine remains unrelated. Mac fast-input, live/human qualification and the paired-release gates remain open. Next, connect a verified image map and bounded guest-memory backend to an actual x64 execution and EFI call path, then establish device and candidate behavior; this synthetic result alone closes none of those requirements.
