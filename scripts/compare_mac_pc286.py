#!/usr/bin/env python3
"""Compare one caller-supplied original Mac-sized PBM with PC/AT EGA output.

Only original/rights-cleared PBMs are suitable. The 286 image is an original
diagnostic, not a ClasSICK OS, a physical machine, or a compatibility test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PBM_HEADER = b"P4\n512 342\n"
PPM_HEADER = b"P6\n640 350\n255\n"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def floppy_for(pbm):
    if not pbm.startswith(PBM_HEADER) or len(pbm) != len(PBM_HEADER) + 21888:
        raise ValueError("expected a canonical binary 512x342 PBM")
    # Original 286 boot sector. BIOS reads 14+15+14 sectors from successive
    # floppy tracks into 2000:0000; BIOS EGA mode 10h then copies 64 bytes
    # per row into the centered canvas with an 80-byte EGA row stride.
    code = bytearray.fromhex("fa 31c0 8ed0 bc007c fb fc b80020 8ec0")
    branches = []
    for bx, count, cx, dx in ((0,14,0x0002,0x0000), (7168,15,0x0001,0x0100),
                              (14848,14,0x0101,0x0000)):
        code += bytes((0xbb, bx & 255, bx >> 8, 0xb8, count, 2,
                       0xb9, cx & 255, cx >> 8, 0xba, dx & 255, dx >> 8, 0xcd, 0x13, 0x72, 0))
        branches.append(len(code)-1)
    code += bytes.fromhex("b81000 cd10 b80020 8ed8 b800a0 8ec0 31f6 bf4801 b95601")
    code += bytes.fromhex("51 b94000 f3a4 83c710 59 e2f4 b0a8 e680 ebfe")
    failure = len(code)
    code += bytes.fromhex("b0ee e680 ebfe")
    for index in branches:
        offset = failure-(index+1)
        if not -128 <= offset <= 127:
            raise ValueError("loader branch outside short range")
        code[index] = offset & 255
    if len(code) > 510:
        raise ValueError("loader exceeds boot sector")
    image = bytearray(1228800)
    image[:len(code)] = code
    image[510:512] = b"\x55\xaa"
    # PBM's 1 is black; in this EGA profile plane bits set form white.
    image[512:512+21888] = bytes(x ^ 255 for x in pbm[len(PBM_HEADER):])
    return bytes(image)


def compare(pbm, ppm):
    if not ppm.startswith(PPM_HEADER) or len(ppm) != len(PPM_HEADER) + 640*350*3:
        raise ValueError("unexpected PC frame format")
    source = pbm[len(PBM_HEADER):]
    pixels = ppm[len(PPM_HEADER):]
    for y in range(350):
        for x in range(640):
            white = False
            if 64 <= x < 576 and 4 <= y < 346:
                px = x-64
                white = not (source[(y-4)*64 + px//8] & (0x80 >> (px % 8)))
            color = 255 if white else 0
            off = (y*640+x)*3
            if pixels[off:off+3] != bytes((color,color,color)):
                raise ValueError(f"frame mismatch at {x},{y}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boothush", required=True, type=Path)
    parser.add_argument("--pbm", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    exe = args.boothush.resolve(strict=True)
    pbm = args.pbm.read_bytes()
    image = floppy_for(pbm)  # validate before creating an output directory
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    disk, frame = out/"original-ega-diagnostic.img", out/"ega-frame.ppm"
    with disk.open("xb") as stream:
        stream.write(image)
    cmd = [str(exe), "--machine", "pc286", "--floppy", str(disk),
           "--cycles", "8000000", "--timeout-ms", "20000", "--until-post", "0xa8", "--frame", str(frame)]
    result = subprocess.run(cmd,capture_output=True,text=True,timeout=25,
                            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    if result.returncode:
        raise RuntimeError(result.stdout+result.stderr)
    state = json.loads(result.stdout)
    if state.get("status") != "condition_met" or state.get("post") != 0xa8:
        raise RuntimeError("guest did not reach the diagnostic marker")
    if digest(disk.read_bytes()) != digest(image):
        raise RuntimeError("input floppy changed")
    ppm = frame.read_bytes()
    compare(pbm, ppm)
    record = {"status":"pass", "scope":"original bitmap display diagnostic, not ClasSICK execution",
              "boothush_sha256":digest(exe.read_bytes()), "source_pbm_sha256":digest(pbm),
              "floppy_sha256":digest(image), "frame_sha256":digest(ppm),
              "cycle":state.get("cycles"), "canvas_pixels_compared":512*342,
              "margin_pixels_compared":640*350-512*342, "input_floppy_unchanged":True}
    (out/"guest.json").write_text(json.dumps(state,indent=2)+"\n",encoding="utf-8")
    (out/"manifest.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(record,indent=2))


if __name__ == "__main__":
    main()
