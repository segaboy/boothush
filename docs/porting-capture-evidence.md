# Super Tilt Bro port capture observations

These are local BootHush observations, not hardware measurements or proof of gameplay equivalence. No ROMs or screenshots are committed here. The NES beta's source revision is unknown. The Mac controls ROM is an independently generated placeholder probe, not game logic.

## Identities

- BootHush executable SHA-256: `fa0917057dec6dbd6a4b378a0290e67cb75c423281c959674967162562bc2b78` (Windows LLVM-MinGW build).
- User-supplied author-provided NES beta SHA-256: `5d045246f12d424c2c67ecda883c66d8d346f7b5396ed819ddb326f44dcb5aaa`; modeled PAL, mapper 2/submapper 1.
- Port project's generated Mac controls ROM SHA-256: `31b2666be66ac7a02d576998cb9b46aec3a91245d16679877d593748fd581fc6`.

## Captures and traces

A previously recorded instruction-based navigation was translated to cycle events. Neutral and player-one-held runs requested NES captures at cycles `30857027`, `30897027`, and `30937027`. Neutral captures landed at `30857028`, `30897028`, and `30937028`; player-one captures landed at `30857027`, `30897027`, and `30937028`. The neutral completed-frame numbers were 928, 929 and 930. The first two paired images had identical SHA-256 values; the third pair differed. This is an observation at nearby instruction boundaries, not a mapped player state or proof of physical behavior. Each run also captured CPU RAM, PPU and OAM bytes at its observation boundary; the image is the last completed NES frame and is not necessarily simultaneous with those bytes.

A Mac controls run captured VBLs 5, 8, 11 and 14, and the `NST2` diagnostic RAM at each point. The keyboard witness recorded modeled guest input due and key-delivery events:

| Transition | Requested cycle | Accepted boundary | Reply delivery |
| --- | ---: | ---: | ---: |
| Position 0 press | 600000 | 600014 | 628468 |
| Position 0 release | 900000 | 900002 | 953094 |
| Position 1 press | 1200000 | 1200000 | 1228834 |
| Position 1 release | 1500000 | 1500016 | 1553508 |

All four inputs were accepted and no trace records were dropped. A separate hash-pinned integration test checks the guest's final diagnostic RAM, the delivered reply sequence and identical repeated trace/images. This does not establish M0110 physical rollover or timing.

## Reproduction

Build BootHush, then run the suite with local, independently supplied inputs:

```powershell
$env:BOOTHUSH_STB_UNROM = 'C:\Repos\.task-temp\super-tilt-bro-author-unrom.nes'
$env:BOOTHUSH_STB_MAC_PROBE = 'C:\Repos\Not-Super-Tilt-Bro\out\controls-v1.rom'
ctest --test-dir C:\Repos\boothush\out\windows --output-on-failure -R '^boothush$'
```

The test suite generates fresh output under its test directory, protects existing files, checks fixture hashes, and cleans up its captures. The [capture guide](headless-capture.md#frame-series) describes how to retain a separate run and extract its individual frames. ROM and executable hashes must be checked again after changing either input.
