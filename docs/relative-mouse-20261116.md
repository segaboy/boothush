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

## Capture startup failure and explicit unconfined mode

M1R1 owner report: "I clicked in, but the window closed." The session stopped
after 61.234 seconds with `input_error`, zero captures/packets/samples and
unchanged inputs; it did not reach its five-minute limit. This round did not
test guest pointer responsiveness or edges. Original artifacts remain in
`out/qualification-20261116-a/M1R1-mac`; a separate feedback record was added.

An agent-operated diagnostic window reproduced the failure. New diagnostics
identified `ClipCursor`, Win32 error 5 (access denied), after raw registration
and before completed capture. Receipt and guest video:
`out/qualification-20261116-a/capture-agent-probe-a`. The agent raised the owned
window and clicked inside its client; a later F12 attempt found it already
closed. This is agent input, not an owner qualification result.

The explicit `relative-free` CLI mode (`--unconfined-mouse` launcher) registers
foreground relative input without querying/changing cursor confinement or
hiding the host pointer. It never silently replaces confined mode and makes
no desktop permission change. The Windows pointer remains free and visible;
F12 stops input, focus loss releases it, and an in-window held primary press
uses ordinary window capture for drag release. Click outside changes focus.
Precise `relative_mouse_error` operation/code and `relative_mouse_mode` fields
now distinguish failure and configuration. Inactive clicks/shortcuts are ignored.

The published Windows [ClipCursor contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-clipcursor)
documents its window-station access requirement. The
[SetCapture contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture)
defines ordinary foreground button capture and loss to another window.
Only API contracts were consulted; no implementation/sample was imported.

Source-matched active-profile twins have SHA-256
`5d86fb1bec512cf8ea965437004ce33914dcb00c591e994f3497740745e8c4bc`.
Build and validation: `C:/Repos/.task-temp/boothush-relative-free-20261116-a/`.
The production visible-setup branches are tested with local API fakes for
every setup failure, exact error preservation, registration rollback on denied
clipping, normal cleanup, no confinement/hiding in free mode, and ignored
inactive input. Three existing native live-window tests and six focused Python
modules pass, including both modes' exact mouse trace replay and CLI error
reporting. These tests are hidden and do not manipulate desktop input.

The unconfined agent probe `capture-agent-probe-b` reached its 120-second
limit with zero errors and zero input. UI inspection timed out before any
agent action; this is only an idle launch result, not a successful capture
or human test. M1R2 will separately test actual input with the unchanged
enlarged-FIFO Mac ROM. Physical/raw-device behavior and human feel remain open.

## M1R2 sensitivity feedback and adjustable gain

The owner reached all four corners without unexpected closure, but reported
having to move the local pointer "VERY far." M1R2 retained 1210 raw packets,
two starts/releases, 1055 scaled samples and 4704 pin steps, with no pending
input, guest omissions, decoder losses, FIFO overflows or faults. Complete
movement accounting did not mean the sensitivity was acceptable. Session,
owner feedback and a guest-video GIF excerpt remain under
`out/qualification-20261116-a/M1R2-mac`; the derived-media receipt identifies
the excerpt's source and conversion. No desktop pixels were retained.

`--live-mouse-gain` / launcher `--mouse-gain-percent` now selects 25-800 percent
linear gain, default 100. F9 halves and F10 doubles it within those bounds;
the title and JSON identify the setting. Gain acts before integer display-scale
conversion. Signed fractional carry preserves small movements, a gain change
clears only the partial fraction, repeated shortcuts are suppressed, and these
host keys never enter the guest. Out-of-range signed output fails explicitly.
Scaled vectors use the existing replay format; replay never applies gain twice.

Source-matched active-profile twins:
`fe262bc58ae58cb1d9b22ae5f409b71693e6ebf0fd305e4846692a5873588de8`.
Receipts: `C:/Repos/.task-temp/boothush-mouse-gain-20261116-a`.
Four native tests and six focused Python modules pass, including low-gain
fractions, 400-percent conversion, repeat suppression, bounds, overflow
rejection, inactive shortcuts, gain reporting and exact mouse replay.

Amplifying the recorded accepted vectors exposed separate guest FIFO loss
at 2x/4x. ClasSICK's selected 2048-record candidate passes the 1x/2x/4x controls,
paired state/pixel comparison and excessive-burst recovery. Its canonical
guided-human receipt records the failed smaller-buffer controls and new ROM
identity. This is not a universal input-rate or latency guarantee. M1R3 starts
at 400 percent with that candidate; human sensitivity remains pending.
