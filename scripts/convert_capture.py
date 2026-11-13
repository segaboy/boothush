#!/usr/bin/env python3
"""Convert a BootHush capture with bundled FFmpeg or an explicit override."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, help="Optional explicit FFmpeg override")
    parser.add_argument("--input", required=True, type=Path, help="BootHush Y4M, PBM or PPM capture")
    parser.add_argument("--output", required=True, type=Path, help="New .gif, .mp4 or .png file")
    parser.add_argument("--timeout-seconds", type=int, default=120)
    args = parser.parse_args()
    ext = args.output.suffix.lower()
    if ext not in (".gif", ".mp4", ".png"):
        parser.error("output must end in .gif, .mp4 or .png")
    if not 1 <= args.timeout_seconds <= 3600:
        parser.error("timeout-seconds must be between 1 and 3600")
    if args.output.exists():
        print("output already exists", file=sys.stderr)
        return 2
    try:
        # The distributed helper is in <bundle>/scripts; the checkout helper
        # locates the built bundle under out/windows. Never consult PATH.
        base = Path(__file__).resolve().parents[1]
        if args.ffmpeg is not None:
            ffmpeg = args.ffmpeg.resolve(strict=True)
        else:
            candidates = (base / 'ffmpeg' / 'ffmpeg.exe', base / 'out' / 'windows' / 'ffmpeg' / 'ffmpeg.exe')
            ffmpeg = next((path for path in candidates if path.is_file()), None)
            if ffmpeg is None:
                raise ValueError("bundled FFmpeg not found; assemble the BootHush bundle or provide --ffmpeg")
            ffmpeg = ffmpeg.resolve(strict=True)
        source = args.input.resolve(strict=True)
        if not ffmpeg.is_file() or not source.is_file():
            raise ValueError("ffmpeg and input must be files")
        with tempfile.TemporaryDirectory(prefix="boothush-convert-", dir=args.output.parent) as scratch:
            temp = Path(scratch) / ("capture" + ext)
            command = [str(ffmpeg), "-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i", str(source), "-an"]
            if ext == ".mp4":
                command += ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-movflags", "+faststart"]
            elif ext == ".gif":
                command += ["-c:v", "gif"]
            else:
                command += ["-frames:v", "1", "-c:v", "png"]
            command += [str(temp)]
            completed = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, timeout=args.timeout_seconds,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if completed.returncode != 0:
                print("FFmpeg failed: " + completed.stderr.strip()[:2000], file=sys.stderr)
                return 1
            # CREATE_EXCL guarantees an existing destination is never overwritten.
            created = False
            try:
                with args.output.open("xb") as output:
                    created = True
                    with temp.open("rb") as inp:
                        shutil.copyfileobj(inp, output)
            except OSError:
                if created:
                    args.output.unlink(missing_ok=True)
                raise
        print(f"Created {args.output}")
        return 0
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f"Conversion failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
