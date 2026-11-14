# Headless screenshots and recordings

BootHush samples the guest framebuffer directly; it does not capture the Windows desktop or move the physical mouse. `mac128k` and `pc286` are supported. The x64 machine is not implemented yet. Recording is opt-in and does not require `--live` or a visible window. A calling Python process can use `creationflags=subprocess.CREATE_NO_WINDOW` to suppress its console.

## Screenshot

`--frame PATH` captures the final stopped guest framebuffer to a new file: Mac outputs a 512×342 binary PBM; PC outputs a 640×350 RGB PPM. `--frame` can be combined with the video options. For example:

```powershell
$exe = 'C:\Repos\boothush\out\windows\boothush.exe'
$rom = 'C:\ClasSICK\mac-candidate-anchored-20261112-a\classick-live.rom'
$still = Join-Path $env:TEMP ('guest-' + [guid]::NewGuid().ToString('N') + '.pbm')
& $exe --machine mac128k --rom $rom --cycles 65000000 --timeout-ms 20000 --frame $still
```

## Video

`--video PATH` writes an uncompressed YUV4MPEG2 (`.y4m`) stream with no audio. Add `--video-fps` (1–60, default 10), `--video-frames` (1–3,600, default 300), and `--video-start` (absolute guest cycle, default 0) as needed. For example, capture the period after boot:

```powershell
$video = Join-Path $env:TEMP ('guest-' + [guid]::NewGuid().ToString('N') + '.y4m')
& $exe --machine mac128k --rom $rom --cycles 90000000 --timeout-ms 30000 `
  --mouse-dcd-invert 3 --mouse-motion 60000000:32:0 `
  --video $video --video-start 59000000 --video-fps 10 --video-frames 60
```

The samples are taken at the first emulator CPU boundary at or after `start + n * clock / fps` (7,833,600 Hz for Mac, 6,000,000 Hz for 286), before applying input or evaluating a stop at that same boundary. `--video-start` remains absolute when `--input-after` is used. Samples use emulated time, not host wall time; video length is frame count divided by fps, and is not necessarily how long the test took to execute. The selected stop, timeout or cancellation may end capture early. A run ending before `--video-start` yields a header with zero frames. There is no automatic final extra frame; use `--frame` for the exact stop image.

The JSON `video` object reports `format`, `width`, `height`, `fps`, requested `start_cycle`, `frames`, `frame_limit`, `last_cycle` (zero when no frames), and `truncated`. `truncated` becomes true if another frame would be due after the cap; guest execution continues. It does not mean a timeout or early stop has been ruled out: inspect the run's `status` and `cycles`. Video creation or write failure reports an error; incomplete writes are discarded where possible. Captures require a new path and are never appended or overwritten.

The stream is intentionally large: at 10 fps it uses about 5.25 MB/s for Mac and 6.72 MB/s for 286, before conversion. The default limit is 300 frames; raise it explicitly for longer captures and leave enough disk space. PC pixels are converted from the emulator's RGB framebuffer to limited-range BT.601 YUV 4:4:4; Mac's black and white bits become corresponding luma. The sampling and conversion do not establish real-time display accuracy. Capture can consume wall time and affect a tight wall-clock timeout even though guest time is sampled deterministically.

## Frame series

NES and Mac runs can capture up to **32** requested points in one run using `--capture-series NEW_PATH`. Repeat `--capture-cycle N` for modeled CPU-cycle points. NES additionally supports `--capture-frame N` for completed PPU frames, and Mac supports `--capture-vbl N` for modeled VBL count; numbered frame/VBL points start at 1. Choose one axis per run. Values must be at most 4,000,000,000; points are sorted chronologically with command-line order retained for ties. A path and at least one point are required. Mac mouse-trace replay is supported; PC and live input are not supported by this feature.

The output is a bounded concatenation of complete, self-contained images: NES P6 RGB PPM images, or Mac P4 monochrome PBM images. JSON stdout is the manifest; save it alongside the output. `frame_series` identifies the ROM bytes actually loaded and the running executable by SHA-256, axis, readiness mode, total bytes, requested/processed/pending counts, missing images and dropped count. Each processed sample records the requested and effective target, actual guest cycle, completed NES frame or Mac VBL, NES instruction count where applicable, count of inputs accepted before capture, and byte offset and length within the series. Pending points have no image. An NES cycle point before the first completed frame is processed with `image_available: false` and no image bytes; it is not silently delayed. A run stopped before readiness or a later point leaves it pending. The point limit is validated rather than dropping excess requests; the maximum image data is about 5.9 MB for NES and 0.7 MB for Mac. Existing output paths are never overwritten.

`--capture-memory ADDRESS:LENGTH` adds one side-effect-free CPU RAM/ROM snapshot of 1-256 bytes to every processed point. Mac accepts physical RAM only; NES accepts CPU RAM/mirrors `$0000-$1FFF` or mapped PRG ROM `$8000-$FFFF`. NES also supports `--capture-ppu ADDRESS:LENGTH` and `--capture-oam OFFSET:LENGTH`, each 1-256 bytes within the respective address space. CPU, PPU and OAM bytes are sampled together at the listed instruction boundary. The NES image is the **last completed PPU frame**, which can have completed earlier; it is not a claim that every pixel and memory byte represents the same instant. The Mac image is a copy of the live framebuffer at the listed boundary, not a hardware-latched completed frame; a VBL-indexed capture can still reflect modeled mid-draw contents.

Cycle points are absolute unless `--input-after` is used, in which case they are offsets from the first matching readiness boundary. Frame and VBL points are always absolute. A Mac readiness-anchored neutral capture can have no scheduled inputs. Each point is processed at the first observed CPU instruction boundary at or after its target; overshoot can differ between scenarios. The actual frame/VBL count can exceed a requested number when an instruction crosses a boundary. No one-to-one timing relationship is assumed between NES PAL frames and Mac VBLs.

At an NES boundary: regular sampled video, readiness, scheduled memory observations, frame-series capture, stop/limits, input, then instruction. At a Mac boundary: sampled video, fault/cancellation/timeout checks, readiness, frame-series capture, stop/limits, input, then instruction. Thus a capture at the same boundary as scheduled input sees guest state before that input is applied; an input coinciding with a stop is pending. `inputs_accepted_before` and the NES `cycle_schedule`/`instruction_schedule` or Mac `series_inputs` record the scheduled input ordering and actual application cycles. For Mac, these describe emulator-accepted events, not necessarily guest-delivered keyboard replies. File creation and writing occur after guest execution; a write failure reports an error. A completed output can be partial when execution stops early, so check run status and pending counts.

With `--mouse-replay`, capture points use absolute cycles or VBLs and remain before input processing at that boundary. Each processed sample additionally reports `replay_samples_queued_before`, the number of trace samples queued to the mouse bridge before that capture; this is separate from `inputs_accepted_before` and does not mean every queued mouse step has reached the guest. A replay is bounded by its recorded stop cycle, so later capture points remain pending. Keep the original trace and record its SHA-256 with the manifest: the manifest identifies the ROM and executable, not the trace. Replay does not support readiness-relative input.

Example using an original Mac fixture whose RAM `$b00` indicates readiness:

```powershell
$exe = 'C:\Repos\boothush\out\windows\boothush.exe'
$rom = 'C:\path\to\fixture.rom'
$result = & $exe --machine mac128k --rom $rom --cycles 1000000 --timeout-ms 5000 `
  --input-after 0xb00:4:0x1234abcd --key 50000:0:1 --key 100000:0:0 `
  --capture-series C:\path\to\new-series.pbm --capture-cycle 0 --capture-cycle 150000 `
  --capture-memory 0xb00:4
$result | Set-Content -Encoding utf8 C:\path\to\new-result.json
python C:\Repos\boothush\scripts\extract_frame_series.py `
  --result C:\path\to\new-result.json --series C:\path\to\new-series.pbm --output C:\path\to\new-directory
```

The extraction helper validates image offsets, sizes and headers, refuses an existing output directory, and writes `frame-NNN.pbm`/`.ppm` plus a copy of the full manifest. A missing/pending point has no image file. Original ROMs, captures and traces remain local; the helper does not download or alter guest media.

See [Super Tilt Bro port capture observations](porting-capture-evidence.md) for a hash-identified NES beta and independent Mac controls probe run, including the requested versus actual boundaries.

## Convert to GIF, MP4, or PNG

The local BootHush build is bundled with FFmpeg at `out\windows\ffmpeg\ffmpeg.exe`. BootHush invokes this copy directly with no console window; it does not search `PATH` or require Python for conversion. It uses a new temporary output, refuses an existing destination, cleans up after a failed conversion, and imposes a two-minute conversion timeout. Use original captures outside the repository:

```powershell
$gif = Join-Path $env:TEMP ('guest-' + [guid]::NewGuid().ToString('N') + '.gif')
$mp4 = Join-Path $env:TEMP ('guest-' + [guid]::NewGuid().ToString('N') + '.mp4')
$png = Join-Path $env:TEMP ('guest-' + [guid]::NewGuid().ToString('N') + '.png')
& $exe --convert-capture $video --output $gif
& $exe --convert-capture $video --output $mp4
& $exe --convert-capture $still --output $png
```

GIF uses FFmpeg's GIF encoder; MP4 uses H.264 with `yuv420p`, so it is lossy and color resolution is reduced. PNG is lossless for a PBM/PPM screenshot; a PNG converted from video represents its first sampled frame, not necessarily the final guest state. Outputs are silent. Conversion is separate from emulation and cannot change guest execution. The old Python helper is retained and defaults to the bundled executable; `--ffmpeg` is an explicit override.

## Bundle provenance and rebuilding

The current local bundle contains FFmpeg `N-125875-g5d4d3bdc61-20260731`, SHA-256 `851aa8ea5366b5af33c0681cc292d662829c7088e9ac96f6e9b37030e6835ba4`. It was copied from the machine's existing WinGet FFmpeg package, along with that package's GPLv3 license text. The copy is independent of the installed executable and continues to work without a system FFmpeg on `PATH`. `ffmpeg\manifest.json` records the exact executable hash and version/configuration output. The 145 MB binary and generated build directory are not committed to Git; a source-only checkout needs a separate local bundle assembly step. Before distributing the binary externally, review the third-party build's complete license notices and corresponding source obligations; this local packaging is not a redistribution compliance determination.

After building BootHush, assemble the bundle from a reviewed, locally supplied Windows FFmpeg build that includes `libx264` and its license file. No download is performed:

```powershell
$python = 'C:\Users\segab\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$source = 'C:\path\to\ffmpeg.exe'
$license = 'C:\path\to\LICENSE.txt'
& $python C:\Repos\boothush\scripts\bundle_ffmpeg.py --ffmpeg $source --license $license --destination C:\Repos\boothush\out\windows
```

The assembly tool checks the supplied executable's version, copies it beside BootHush, records hashes, and refuses to replace a different existing bundled executable. It also copies the compatibility helper. To run the optional encoder/decoder tests, set `BOOTHUSH_TEST_FFMPEG` to `C:\Repos\boothush\out\windows\ffmpeg\ffmpeg.exe` before running CTest. The native bundled-conversion test runs when the adjacent bundled executable is present.
