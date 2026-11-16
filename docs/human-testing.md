# Human testing in BootHush

BootHush remains headless by default. The owner's 2026-11-16 direction adds an explicit human testing path for the Mac/286 pair. `--live SCALE` opens an owned Windows window and paces guest execution to its modeled clock. It does not install global input hooks or move the host pointer. The window appears without taking foreground focus; click it to activate. Close it or reach the execution limit to finish.

| Profile | Display and human controls |
| --- | --- |
| `mac128k` | 512×342 monochrome, relative mouse movement and primary-button dragging, plus M0110 keyboard transitions. Use the guest pointer to aim. The first activation click is consumed. |
| `pc286` | 640×350 EGA graphics, or diagnostic text with a supplied `--ega-font`; Set 2 keyboard make/break packets. Host mouse movement is not connected to a guest mouse. |

Both profiles map A–Z, digits, space, Return, Backspace, Tab and US punctuation keys except backtick. The 286 also maps arrows, Escape, left/right Shift and Control, including extended arrow/right-Control packets. Mac modifiers, arrows, keypad and function keys are currently unsupported. This covers the port's letter-based gameplay controls; it is not a complete keyboard or international text-input implementation. Alt/Windows shortcuts remain host shortcuts. Physical keyboard rollover is host-dependent.

Held keys receive releases on focus loss, cancellation, minimization and close. A key already held before activation must be released and pressed again. Mac repeats are suppressed; 286 repeats use Windows typematic events. Focus-loss release affects keyboard state: it cannot undo a guest's toggle action. For example, the current ClasSICK 286 preview uses arrows for 16-pixel pointer steps and a Space press to toggle the primary button. Tap Space again to release before changing focus; holding Space can repeat the toggle.

The Mac mouse captures a held primary button until release and clamps captured motion at viewport edges. Leaving and reentering without a held button resets its relative anchor. The existing window has fixed integer sizing and no caption-drag/resize loop; scale is 1–4 and reduced when needed to fit the primary work area. It can be minimized and closed. No NES live mode, audio playback, save states, or physical-hardware qualification is added.

## Launch and retain evidence

From PowerShell, with Python 3.11 or later, choose existing images and a new output directory:

```powershell
python scripts/run_human_session.py --emulator C:\path\boothush.exe `
  --machine mac128k --rom C:\path\replacement.rom --output C:\path\mac-session `
  --seconds 120 --scale 2 --record-seconds 30 --ffmpeg C:\path\ffmpeg.exe `
  --mouse-dcd-invert 3 --mouse-edge-cycles 2048 --mouse-vbl-guard 4096

python scripts/run_human_session.py --emulator C:\path\boothush.exe `
  --machine pc286 --hard-disk C:\path\preview.img --ega-font C:\path\diagnostic-8x14.bin `
  --output C:\path\pc-session --seconds 120 --record-seconds 30 --ffmpeg C:\path\ffmpeg.exe
```

The shown Mac settings are for the buffered ClasSICK candidate; choose the settings supported by the particular ROM's evidence. Launcher defaults match BootHush: edge 8192, inversion 0, VBL guard 4096. `--inspect ADDRESS:LENGTH` can be repeated for guest diagnostics. A PC can use `--rom`, `--floppy` or `--hard-disk`; the built-in firmware boots floppy first. Mounted media remain unchanged and the launcher does not save guest disk writes. Use the explicit CLI `--disk-out NEW_PATH` separately for an installation test requiring persistence.

Append `--prepare-only` to write `plan.json` and `launch.ps1` without launching. Run `& C:\path\plan\launch.ps1` when ready. It verifies the reviewed image, executable, Python and launcher hashes and creates a new `session` subdirectory. Existing destinations are rejected; prepare another directory to repeat. Preparing or launching a session is not a pass result.

Every completed launcher run retains the emulator command, input hashes, JSON stdout, stderr, final PBM/PPM, accepted-key log, and `session.json`. Mac additionally records `mouse.bhmouse`. Optional video records the first requested guest seconds at 5 fps, bounded independently of the session. With an explicit FFmpeg path, the helper converts the final frame to PNG and video to MP4 and retains conversion commands/results. Y4M is large (about 3.36 MB/second for PC at 5 fps); recording is off by default. Capture timing is guest time, not necessarily wall time. The [capture guide](headless-capture.md) defines those limits.

Fill in `observations.md` after testing: tester/date, actual actions, expected and observed results, focus/release/exit behavior, capture references, defects, and disposition. `human_verified` remains false in machine-generated reports because Windows messages can be synthetic. A tester's completed record supplies the separate human evidence. Screenshots alone do not establish input reliability, physical-hardware fidelity, or release qualification.

## Input accounting

`--live-key-log NEW_PATH` requires `--live` and refuses existing files. The text header is `BHLIVEKEY1 PROFILE STOP_CYCLE COUNT PENDING`, followed by column names and decimal rows: `cycle code pressed extended repeated focus_release`. Mac codes are the existing BootHush M0110 API codes (0–0x39); PC codes are Set 2 packet bodies. PC packets are E0 if extended, F0 if released, then code. Rows preserve acceptance order when cycles are equal. A PC packet waits until all its bytes fit in the controller output queue.

The live JSON object labels `input_source: host_window_messages` and reports accepted keys/bytes, synthetic focus releases, deactivations, pending window keys and logged rows. Acceptance is into the modeled device, not proof that the guest consumed it. Pending controller bytes and guest behavior need separate guest observations. There is no automatic keyboard-log replay command; the mouse trace alone cannot reproduce a session with live keys. Scheduled Mac keys may coexist for diagnostics and their count is reported; the human launcher supplies none. PC live input refuses scheduled scan codes.

The window holds 512 keyboard events, processes bounded message batches and reserves capacity for focus releases. The optional log holds 4096 accepted transitions; saturation stops with `input_overflow`. Mac device queue rejection also stops visibly. A close may leave unapplied transitions when a guest is not draining input: inspect `pending_keys` and stop status. Timeouts and early exits retain available evidence but are not successful human-test outcomes merely because files exist.

## Automated verification

`live_keyboard` tests hidden Mac/286 window dimensions, overlapping key presses, repeats, startup-held rejection, focus releases, extended arrows, close, queue overflow, and offscreen RGB channel/row rendering. `live_keyboard_cli` uses original ROMs and hidden-window messages to verify actual guest RAM receives A/F press/release bytes through each modeled keyboard. `human_session` checks preparation/no-overwrite and collection of a hidden fixture's logs, frame and video without inventing human attestation. The existing Mac mouse CLI fixture still checks exact recorded/replayed guest state and pixels.

All these tests remain invisible and use no physical input injection. Their evidence supports the implementation; actual human qualification of ClasSICK and the port is a separate next step.

The [2026-11-16 receipt](human-testing-evidence-20261116.md) identifies the
tested build, candidate regression results, prepared local launchers and
automated capture artifacts.
