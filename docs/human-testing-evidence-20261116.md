# Human-testing capability receipt — 2026-11-16

This implements the owner's request for opt-in human testing while retaining
headless automation. It does not qualify the paired release or accept its
remaining gaps. See [human testing](human-testing.md) for controls, commands,
and evidence limits.

The active-profile twin build produced identical executables with SHA-256
`f127e61e5507b50e8d109a0788344909305b6d3933cf0c2711a2feb2b810b8a8`.
The build receipt is
`C:/Repos/.task-temp/boothush-human-20261116-b/report.json`. Mac/286/NES were
rebuilt; the existing pinned x64 media archive was linked unchanged, and
paused x64 guest work was not built. Existing unrelated working changes
were preserved. This is an engineering build, not a source-complete release.

The ready local executable is
`C:/Repos/boothush/out/human-testing-20261116/boothush.exe`. Prepared launchers
in that directory are `classick-mac/launch.ps1`, `classick-286/launch.ps1`
and `not-tilt/launch.ps1`. They check input hashes, then open a visible
120-second session with up to 30 guest seconds of video, logs and a final
PNG. No visible session was started in producing this receipt. Each prepared
launcher creates its own new `session` directory and refuses to overwrite it.

## Executed validation

- The hidden native-window tests passed key mapping, overlapping presses,
  startup-held rejection, Mac repeat suppression, PC repeat handling,
  focus-loss release, close, overflow and PC RGB rendering checks.
- The CLI fixture verified original Mac and PC guests receive A/F presses
  and their releases in guest RAM. Existing Mac mouse record/replay still
  matches exact state and framebuffer bytes. Key-log overwrite was refused.
- Python discovery across 23 active test modules ran 162 tests: 143 passed
  and 19 optional fixture tests skipped. The paused x64 module was excluded.
  Launcher preparation, output protection, input hashes, accepted-key logs,
  screenshot/video collection and false human-attestation defaults passed.
  Commands and logs: `C:/Repos/.task-temp/boothush-human-20261116-b/validation.json`.
- With the production executable, all eight existing 286 scene checkpoints
  retained their exact reported state and pixels. The Mac parity run retained
  all captured bytes and its mouse trace; only the intentionally changed
  executable identity differs in the report. Both players' early-held
  recovery-jump cases likewise retained exact observations, frames and
  keyboard witnesses. Receipt:
  `C:/Repos/.task-temp/boothush-human-guests-20261116-a/report.json`.

## Actual candidate capture checks

A separate test-only executable replaces window creation with hidden windows
and injects A/F presses, focus-loss releases and close. Its bounded timer was
delayed to allow the retained ClasSICK candidates to boot. This is synthetic
window-message evidence, not a person playing or operating either guest.

| Candidate | Guest cycles at close | Accepted keys / bytes | Focus releases | Pending window keys | Video frames at 5 fps |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mac buffered ROM | 119,002,570 | 4 / 4 | 2 | 0 | 76 |
| 286 preview disk | 13,087,560 | 4 / 6 | 2 | 0 | 11 |

Each launcher reported completion, unchanged inputs, and successful PNG/MP4
conversion. Both final PNGs were visually inspected: the expected ClasSICK
window scene is visible, with the 286 canvas centered within EGA margins.
Exact commands, fixture executable hash, media hashes, input logs, image/video
hashes and JSON diagnostics are retained under
`C:/Repos/boothush/out/human-testing-20261116/automated-mac/session.json`
and `automated-pc/session.json`. The human observation templates remain
incomplete. The window's PC pixel conversion also has an independent
offscreen red/green/blue corner test; these guest captures do not claim to
be desktop screenshots.

The frozen paired review archive remains unchanged. Human usability and
release disposition remain for the next qualification session. The port
remains a prototype, and no physical Macintosh or 286 was tested.
