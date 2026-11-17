# Raw, guest and host pointer observations

The M1R5 ClasSICK human round still felt slower than the Windows pointer,
with a perceived X/Y difference. It also exposed 27 guest motion omissions
at edge spacing 2048. Replaying the same trace reproduces the omissions;
spacing 4096 removes them in that replay. Neither result establishes why
the owner perceives different gain. The frozen review archive remains intact.

BootHush now accepts `--live-pointer-log NEW_PATH` with a live relative Mac
mouse mode. The human launcher exposes `--pointer-log`. This records input
before the quadrature bridge: raw relative counts, scaled guest vectors, and
sampled Windows-pointer displacement, with elapsed host milliseconds,
capture number, actual viewport scale and selected gain. Logging does not
change scaling or write guest RAM. Absolute desktop positions are not saved.

`BHLIVEPOINTER1 COUNT DROPPED` precedes the column header:
`elapsed_ms capture raw_dx raw_dy guest_dx guest_dy host_dx host_dy host_valid scale gain_percent`.
There are at most 65536 retained records. Further records increment DROPPED
without changing input delivery. An existing file is never overwritten.
Zero scaled movements remain in this log; the mouse replay trace retains
only accepted guest movements/buttons. Host elapsed time and guest cycles
are different clocks; this log is not itself a replay format.

The host pointer is sampled when a raw packet is processed, so coalescing,
queued messages, desktop-edge clamping and cursor warps can make an individual
sample differ from that packet's processed movement. Compare complete
controlled sweeps, not individual rows. `host_valid=0` means there was no
continuous host-position baseline; host deltas are then zero placeholders.
Capture/release and unavailable position reads reset that baseline. Hidden
fixtures never sample the physical pointer and label host data unavailable.

The existing linear conversion remains `raw * gain_percent / (100 * scale)`
with independent signed fractional carry for each axis. A gain change or
capture boundary clears that carry. Both axes use the same denominator.
Windows' published [RAWMOUSE API contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse)
states that raw input does not follow Control Panel mouse-speed scaling.
That is an API distinction, not proof of this owner's acceleration setting,
device behavior or the cause of the reported axis difference. Only published
API documentation was consulted; no Microsoft implementation or sample code
was used. No host mouse setting, clipping permission or cursor position is
changed by this diagnostic.

The original hidden tests exercise raw/scaled equality, host baseline failure
and recovery, recapture, truncation, unchanged delivered input and CLI
record/replay. Actual human response remains unqualified.

## Validation and next human round

On 2026-11-17 the active-profile build produced identical executable twins,
SHA-256 `9dc2f28425bafd10114cfde9ab5c439d61234873854cfa1c211cdcfb26bda82d`.
Build inputs, commands and tool identities are retained in
`C:/Repos/.task-temp/boothush-pointer-log-20261117-b/report.json`.
Mac, 286 and NES sources were rebuilt; the existing x64 media archive was
linked unchanged. This is an experimental local test executable, not a
source-complete distribution or a new third-party licensing review.

Four C++ executables passed: capture setup with mocked host API calls,
live keyboard, live Mac window and relative mouse behavior. Six Python suites
discovered 50 cases: 47 passed and three optional ClasSICK-ROM cases skipped
because that fixture was not supplied. The separate actual ClasSICK trace
replays below supply the pertinent guest regression, not those skipped tests.
`validation.json` in the build directory pins commands and outcomes. Tests
remained hidden; none moved or captured the owner's physical pointer.

The final executable repeated M1R5 at edge spacings 2048 and 4096 with the
same respective guest results and exact final pixels as the old executable.
Receipt: `C:/Repos/.task-temp/m1r5-pointer-build-20261117-b/report.json`.
That checks replay behavior; it does not test actual host-pointer sampling.

A guarded five-minute M1R6 plan is prepared under
`C:/Repos/Release-Review/ClasSICK-Mac286-Cursor-20261116/M1R6-pointer-20261117/`.
It retains 2x zoom and 300-percent gain, enables pointer logging, and uses
4096-cycle edges as the replay-tested omission control. The unchanged ROM is
`5d48f7877386e30f8e45e0456f528e93c1990aa7dd5b7ef37b40ddd7cf1de5a0`.
Short horizontal and vertical out-and-back sweeps, at slow and quicker pace
with pauses, are proposed. Avoid host/guest edges and sensitivity changes so
complete sweep distances can be compared. The guest recording alone cannot
show the Windows pointer. No human result is asserted here.

An updated P1R2 286 plan uses the unchanged packaged disk and font. Earlier
prepared launchers remain preserved and correctly refuse the changed launcher
hash. P1R2 is prepared only; the Mac diagnostic is the immediate task.
