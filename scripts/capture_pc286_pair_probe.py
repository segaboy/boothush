#!/usr/bin/env python3
"""Capture original 286 EGA cursor diagnostic frames in a new output directory."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boothush", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    module_spec = importlib.util.spec_from_file_location("pair_probe", root / "tests/test_pc286_pair_canvas.py")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    exe = args.boothush.resolve(strict=True)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    disk = out / "original-cursor-diagnostic.img"
    with disk.open("xb") as stream:
        stream.write(module.make_keyboard_cursor_floppy())
    disk_hash = sha(disk)
    captures = []
    for marker, label in ((0xa2, "right"), (0xa3, "left")):
        frame = out / f"{label}.ppm"
        command = [str(exe), "--machine", "pc286", "--floppy", str(disk),
                   "--cycles", "8000000", "--timeout-ms", "20000",
                   "--scancode", "2000000:0x74", "--scancode", "2100000:0xf0",
                   "--scancode", "2101000:0x74", "--scancode", "3000000:0x6b",
                   "--until-post", hex(marker), "--inspect", "0x600:4", "--frame", str(frame)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=25,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode:
            raise RuntimeError(f"{label}: {result.stdout}\n{result.stderr}")
        state = json.loads(result.stdout)
        expected = "611f0100" if label == "right" else "601f0200"
        if state.get("status") != "condition_met" or state.get("post") != marker or state.get("memory", {}).get("bytes") != expected:
            raise RuntimeError(f"{label}: unexpected guest state")
        (out / f"{label}.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
        captures.append({"label": label, "post": marker, "frame": frame.name, "frame_sha256": sha(frame),
                         "guest_memory": expected, "cycle": state.get("cycles")})
    if sha(disk) != disk_hash:
        raise RuntimeError("input diagnostic image changed")
    report = {"machine": "pc286", "kind": "original cursor diagnostic, not ClasSICK",
              "boothush_sha256": sha(exe), "fixture_sha256": disk_hash,
              "test_source_sha256": sha(root / "tests/test_pc286_pair_canvas.py"),
              "captures": captures, "input_media_unchanged": True}
    (out / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
