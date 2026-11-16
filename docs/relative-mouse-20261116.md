# Relative mouse capture after the first human round

2026-11-16. The owner completed the first ClasSICK Mac human test and reported
lag and inability to reach all boundaries, while drag, release and content
pattern changes worked. The canonical cross-project human record is
`C:/Repos/ClasSICK-0.97/docs/development/guided-human-qualification-20261116.md`.
The original session remains at `out/qualification-20261116-a/M1-mac`.

The legacy window computes deltas between successive host client positions.
Its anchor need not match the guest cursor, and host travel ends at the client
edge. The new explicit `--live-mouse relative` path consumes foreground relative
RAWMOUSE packets instead. A capture click is swallowed; F12/focus loss/close
release input. The cursor is confined to the owned client only while captured,
and clipping is restored when released. No desktop input hook, background
input subscription or guest RAM cursor override is used.

Raw packet counts are divided by actual integer display scale with signed
fractional remainder. Existing legacy motion is ignored to avoid duplication.
Windows primary-button messages are retained; hardware acceleration and
absolute raw devices are not emulated. Unsupported/read/capture errors stop
explicitly. Normal `window` input remains the default for compatibility.

The implementation uses the Windows SDK API declarations and the documented
foreground/cleanup contracts in Microsoft's [WM_INPUT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input),
[RAWINPUTDEVICE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice)
and [RAWMOUSE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse)
API references, consulted 2026-11-16. No third-party implementation was imported.

Active-profile executable twins match SHA-256
`ab6f2d3820a326d681b9902a2a5439d043176af2bee19cfe3a72d0490fd76a6e`.
Build and validation receipts:
`C:/Repos/.task-temp/boothush-relative-20261116-a/report.json` and
`validation.json`. Hidden tests cover fractional deltas, travel beyond both
viewport dimensions, ignored legacy duplicates, capture-click suppression,
primary press/focus release, F12 repeat handling, recapture/close cleanup and
unchanged host clipping. A test-only CLI verifies scaled movement enters the
recorded mouse trace and reproduces guest RAM/pixels through headless replay.
All 23 active Python modules pass with the existing optional skips; x64 work
remains paused. Hidden packet injection does not prove physical raw delivery.

The paired native scene passes eight state/pixel checkpoints with this build
and the experimental enlarged-FIFO Mac ROM. The recorded human input replays
without guest omissions or FIFO loss at edge interval 4096. That improvement
belongs to the distinct ClasSICK change; this host mode alone cannot repair a
guest that cannot consume the input. Human responsiveness, actual raw delivery
and complete boundary reach await the next owner-operated round.
