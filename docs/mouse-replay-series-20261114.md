# Mac mouse replay frame-series checkpoints

2026-11-14. ClasSICK's paired-target comparison needs to replay a recorded
modeled mouse sequence and compare every intermediate Mac frame and guest-state
snapshot. Previously the CLI rejected replay combined with frame-series capture.
BootHush now allows this headlessly, with a separate count of trace samples
queued before each capture. [Capture semantics and limits](headless-capture.md)
explain ordering, pending points and trace identity. This is a test facility,
not evidence of physical mouse behavior.

The isolated Release build used the existing local LLVM/MinGW toolchain and
SHA-256 `5d4f610e98cbb8b03476f28929d6e5c0f64b941aafac3c7d5b6ef12c76c7eeb3`.
The `test_mouse_cli` and `test_video_capture` Python suites ran 18 tests with
15 passing and 3 optional fixture/FFmpeg tests skipped. The new replay test
checks repeated image and state equality, unchanged guest result, the
input-boundary order, capture past the recorded stop, VBL indexing, refusal to
overwrite, and refusal of readiness-relative replay. Output and traces are
kept outside Git. Run using `BOOTHUSH_EXE` pointed at the built executable and
`python -m unittest -v test_mouse_cli test_video_capture` in `tests`.

The local integration run `C:/Repos/.task-temp/mac-pc286-parity-20261114-c`
compares seven ClasSICK Mac frame/state checkpoints with its separate 286
experiment and then replays the Mac trace. The frame bytes, memory and actual
checkpoint cycles match the recording. The port's separate title-motion test
under `C:/Repos/Not-Super-Tilt-Bro/out/title-replay-test-20261114-a` matches
22 recorded and replayed frames and memory snapshots. See each project's
records for revisions, command lines, hashes and qualification limits. Neither
result proves physical input, production boot, a playable game or a release.
