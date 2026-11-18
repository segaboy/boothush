# Explicit private floppy writes and snapshots

2026-11-18. `--floppy-out NEW_PATH` enables writes to a private copy of the
supplied floppy. The source is never opened for writing. A snapshot is created
only after a successful bounded guest run and successful requested observations.
Without this option, inserted floppies retain their existing protected behavior.

An existing output file or directory, including the input path, is refused
before guest execution. The final write also uses `CREATE_NEW`, so a destination
created during execution cannot be overwritten. Failed writes use the existing
new-file helper's delete-on-failure handling. Cancellation, timeout, an unmet
stop condition, failed capture/witness output, or an invalid/incomplete Mac
write prevents the new floppy snapshot. Images remain outside Git.

This provides modeled sector persistence for provider development. It does
not qualify filesystem consistency, System Disk startup, a product storage
bridge, historical firmware compatibility, human use or physical hardware.

## Mac sector-record contract

The source may be a raw 409,600-byte image or a previously valid DC42 container
with zero or 9,600 tag bytes. The output retains its exact container format and
header metadata. DC42 data and tag checksums are recalculated at export; the
first sector's 12 tags remain excluded from the DC42 tag checksum as specified
by the existing container model. Other sector bytes and tags remain unchanged.
Raw and untagged DC42 cannot represent nonzero tags: a record containing them
is rejected before any sector mutation. Tagged DC42 accepts and preserves them.

The guest selects the internal drive, turns its motor on and seeks the desired
track through the existing switches. Drive sense command 9 then reports the
no-write-protection signal high. Raise Q6 and Q7; Q7's rising edge opens a new
bounded record session. The write path accepts a synchronized record for the
current track, independent of its rotational offset:

| Record bytes | Meaning |
| --- | --- |
| 0-2 | `d5 aa ad` data prologue |
| 3 | GCR symbol for the zero-based sector in the current speed zone |
| 4-702 | 699 GCR symbols encoding 12 tag bytes followed by 512 data bytes |
| 703-706 | Four GCR checksum symbols |
| 707-708 | `de aa` epilogue |

For each byte, read register 12 (Q6 low, Q7 still high), then write the byte
to odd register 13 (Q6 high). The ready cadence is one accepted byte per
124 guest cycles. This uses the existing IWM nibble cadence, not a measured
physical write timing claim.

| Handshake | Meaning |
| --- | --- |
| `ff` | Bit 7 ready, bit 6 healthy: a byte can be accepted |
| `7f` | Bit 7 clear, bit 6 healthy: wait, with a guest poll bound |
| `3f` | Bit 6 clear: current writable session rejected a strobe or record |
| `bf` | Legacy protected mode: bit 6 clear; do not write |

Check bit 6 before waiting for bit 7. The last byte validates the entire
record and commits the sector, tags and encoded readback together. Poll for
the final healthy/ready acknowledgement, then read register 14 to lower Q7.
Closing an incomplete record rejects it. Sync/gap bytes outside data records
may be skipped, but a session containing bytes without any complete record
is rejected when closed. This is not arbitrary track writing or formatting.

Geometry, GCR symbols, rolling checksum and epilogue must validate before any
media byte changes. Busy, not-ready and changed-track strobes are rejected.
A fixed 709-byte staging buffer bounds record memory. Completed records can
be followed by another record in the same session. Sector commits are atomic
within this model; multiple sector writes are not one atomic transaction.
If a later record fails, earlier private commits remain readable in the guest,
but the CLI refuses the entire requested export for that run. A new Q7 session
clears the per-session handshake error; the lifetime rejection count stays
sticky and still prevents export. An open gate with accepted data also prevents
export, even if its last sector was complete.

With `--floppy-out`, Mac JSON includes `mac_floppy_write`: `enabled`,
`error`, `gate_open`, `pending_bytes`, `accepted_bytes`, `committed_sectors`
and `rejected_records`. `error` includes any earlier rejection in this run;
it is broader than the current handshake latch. These are modeled controller
observations, not guest filesystem success. Existing `mac_floppy` observations
now report the opted-in medium as unprotected. Default JSON remains unchanged.

Internal APIs are `mac128k_enable_floppy_writes`, `mac128k_export_floppy`,
and `mac128k_read_floppy_write_state`. Export returns borrowed image bytes
only when the private write state permits a snapshot. Insertion still starts
protected and validates the complete source before replacing existing state.

## Native 286 scope

The option requires built-in firmware: supplied system ROMs are refused for
this writable lane. `INT 13h/AH=03h` may update the existing private raw floppy
copy after the complete count, CHS, track, 64 KiB DMA boundary and conventional
RAM range validate. Default requests retain status `03h` and do not change
media. Invalid CHS/count returns `04h`, buffer errors `09h`, and a missing drive
`01h`, with carry set and no transfer. Success reports the transferred count.
The existing [firmware contract](pc-at.md#built-in-firmware-contract-for-disk-development)
defines the remaining calls. Direct FDC writes and formatting remain unsupported;
the FDC's protected sense is unchanged. Fixed-disk behavior is untouched.

`pc286_enable_floppy_writes` rejects null machines, missing media and supplied
firmware. `pc286_floppy` returns borrowed raw image bytes. The CLI persists the
1,228,800-byte copy after the normal successful-run gate. A guest that handles
an invalid BIOS write and then stops successfully can export its unchanged
copy; the BIOS result and guest evidence determine operation success.

## Qualified build

Base HEAD: `99ba258f05d3cc96317b951c159df3029c203c16`, plus this working change.
Two fresh active-profile builds produced identical executable SHA-256:
`3d48d7515d3492ac796d3899ed2c47603d3330e3f0f64352fd29ed6a78284ff4`.
The executable is
`C:/Repos/.task-temp/boothush-floppy-writes-20261118-b/a/boothush-active.exe`;
the matching second copy is under `b/boothush-active.exe` in that output root.

| Source/test | SHA-256 |
| --- | --- |
| `src/iwm.cpp` | `10cce081909f36661ae144d93ff3b078363076efd87934ca21d7267875426155` |
| `src/iwm.h` | `abd4b275328b619b510ebd025b7b967246bec4773d5d93872c804a1ff057fc55` |
| `src/mac128k.cpp` | `4ca550e82065b153e83b8c6f9731f01c8c6e2bd1bc0ef3ae8a9559e29b28d843` |
| `src/mac128k.h` | `cf0d8f372f6acbe309d6273ab41bc09b825d79cc34be8876560684e1fc19e2ba` |
| `src/main.c` | `ce02258c153172e7423d6b7433a6982cf5cfdfdf24133fa3984b1a4fd1b3bc0c` |
| `src/pc286.cpp` | `d1bfe30af468878d22bbb4906ccd914b5b1967b71ac8bec452dfa6fdadb12816` |
| `src/pc286.h` | `ad30569026382cf78840a477091bced39157fda157ae98020a69c51c729f186f` |
| `tests/test_mac_floppy_write.py` | `db9e2f3a9ea7864b7f948224f09360c59b918bac34eddd05dbc53090c0e5ccfc` |
| `tests/test_pc286_floppy_write.py` | `a560ed2df43443ce9c925cad599eee523f4040f0929005267c459bb115955e3f` |

The output root's `report.json` pins source/tool inputs before and after the
twin build, exact commands, objects and libraries. `qualification.json` pins
the final test sources, Python tool and logs and rechecks all build inputs.
The [active-profile build limitation](active-profile-build-20261115.md) remains:
active sources are rebuilt, with the pinned existing x64 media archive linked
unchanged. This is not a source-complete distribution or third-party review.
Pre-existing CMake/x64 work was preserved. No third-party CPU body or owner
media was inspected for this change; the existing IWM attribution still applies.

The first build directory, `boothush-floppy-writes-20261118-a`, retains a
compile failure from one missing null argument in the generic result reporter
after adding the optional write diagnostic. The corrected source is qualified
in `-b`; no failed build is substituted into evidence. The existing help-string
overlength warning remains; no new warning class was introduced.

## Tests and controls

The final combined run collected **104 tests in 27.135 seconds: 100 passed,
4 optional ClasSICK-ROM cases skipped**. It includes six new Mac methods,
four new 286 methods, existing floppy readers/checksum injection, and shared
boot, run controls, development controls, mouse witnesses, CPU observations
and long-cycle regressions. Original 20-billion Mac and beyond-32-bit 286
loops still stop with the same guest results as the preceding qualification.

Mac tests prove same-guest write/read and a fresh protected boot from raw,
untagged DC42 and tagged DC42 exports. Exact whole-container comparisons cover
tracks 0, 16 and 64, tags including sector zero, opaque metadata, untouched
sectors and regenerated checksums. Two complete records in one gate also
persist correctly. Invalid geometry, GCR symbols, checksum, epilogue, truncated
or missing records, nonrepresentable tags, busy/not-ready strobes and an unclosed
gate preserve input and cannot export. A valid first record followed by a bad
or truncated second proves the snapshot is suppressed despite the earlier
private commit. Existing-path/input-path, duplicate/missing options, failed
run, actual timeout, failed frame output and invalid DC42 controls leave no
new snapshot. Protected legacy reads and ignored writes still pass.

The native286 suite executes 20 original guest boots across four methods:
single/multi/full-track writes, exact boundary cases, thirteen invalid request
cases, status/carry/BDA observations, source hashes and complete snapshot byte
comparisons. A separate fresh protected boot reads the exported content.
The ten existing PC286 tests also pass. Separate logs are `pc-write-tests.log`,
`pc-existing-tests.log` and `pc-write-tests.json` in the output root.

The earlier `11f594b4f1ef0a1ef805c776364710713e60fa17440b16273096b6a194cb57e5`
executable rejects `--floppy-out`; the new roundtrip method fails its four
format/track subcases against that old binary, as expected. This refusal is
retained in `old-readonly-control.*`. The final logs are `qualification.stdout`
and `qualification.stderr`; the earlier 18-test Mac pass is in
`mac-write-tests.*`. No historical System image or visible UI was used.

## Reproduce

From `C:/Repos/boothush`, build into a new directory:

```powershell
$py = 'C:/Repos/ClasSICK-0.97/.tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/python/bin/python.exe'
$tools = 'C:/ClasSICK/bootstrap-clean-source-verified/.tools'
& $py -B scripts/build_active_profiles.py --cmake "$tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe" --ninja "$tools/ninja/ninja.exe" --compiler-bin "$tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/bin" --x64-media-archive C:/Repos/.task-temp/boothush-replay-series-20261114-a/libboothush_x64_media.a --output C:/Repos/.task-temp/NEW-FLOPPY-WRITES
```

For the recorded binary, from `C:/Repos/boothush/tests`:

```powershell
$env:BOOTHUSH_EXE = 'C:/Repos/.task-temp/boothush-floppy-writes-20261118-b/a/boothush-active.exe'
$env:BOOTHUSH_TEST_TMP = 'C:/Repos/.task-temp/boothush-floppy-writes-20261118-b'
$env:BOOTHUSH_LONG_CYCLE_TESTS = '1'
$env:BOOTHUSH_CLASSICK_ROM = $null
$env:BOOTHUSH_CLASSICK_WINDOW_ROM = $null
$env:BOOTHUSH_CLASSICK_LIVE_ROM = $null
& $py -B -m unittest -v test_mac_floppy_write test_mac_floppy test_pc286_floppy_write test_long_cycles test_boot test_run_controls test_development_controls test_pc286 test_mouse_witness_cli test_mac_cpu_state
```

The helpers `data_record` and `write_rom` in the new Mac test are original
reproducible host encoder and guest handshake fixtures. Broader ClasSICK
provider tests must independently pin this emulator and their own sources,
media, budgets, observations and error handling.
