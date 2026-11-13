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
