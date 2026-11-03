#!/usr/bin/env python3
"""Package the 720p fbscale test with all eight cores (the SMP test, tools/build_daisy.sh's note)."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess

import adt

ROOT = Path(__file__).resolve().parents[1]


def properties(data):
    root, used = adt.parse(data)
    assert used == len(data)
    found = {}

    def visit(node, parent):
        path = parent + "/" + node.name
        for key, flags, value in node.props:
            if key != "random-seed":
                found[path, key] = flags, value
        for child in node.children:
            visit(child, path)

    visit(root, "")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path, help="kernel.development.vince with adjacent SHA256SUMS")
    parser.add_argument("output", type=Path, help="new output directory")
    args = parser.parse_args()
    kernel = args.kernel.resolve()
    data = kernel.read_bytes()
    checksums = {line.split()[1].lstrip("*"): line.split()[0]
                 for line in kernel.with_name("SHA256SUMS").read_text().splitlines()}
    assert checksums[kernel.name] == hashlib.sha256(data).hexdigest()
    assert b"VinceGPU: metal=1: accelerator names HakiosMetal plug-in" in data
    assert b"DAISY HID: enabled" in data
    assert b"DAISY GPU CLOCK: configured 650 MHz" in data
    reference = ROOT / "artifacts/daisy-gpu-rendersmoke-0208"
    baseline = ROOT / "artifacts/daisy-touch-hid-0194-smp/daisy-boot.img"
    assert hashlib.sha256(baseline.read_bytes()).hexdigest() == "a6678e5576c3376c4424704c10d65aab7fbe13c7aca7546fe49cea12a9c75fc1"
    assert hashlib.sha256((reference / "daisy-boot.img").read_bytes()).hexdigest() == "daecd1dbf3bb0fe5b1f55facebe3195eb6b2526df085465c8336ec26c2eda4cc"
    previous = (reference / "daisy.payload").read_bytes()[0x48:0x48 + 608].split(b"\0")[0].decode().split()
    assert {"adreno=11", "daisygpu_test=1", "daisygpu_mhz=650", "daisygpu_pool=512",
            "daisysurface_dma=1", "glfetch=0"}.issubset(previous)
    switches = {"gl", "glblit", "rendersmoke", "metalsmoke", "metal", "fbw", "fbh", "cpus", "daisylog"}
    cmd = " ".join([arg for arg in previous if arg.split("=", 1)[0] not in switches] +
                   ["cpus=8", "fbw=750", "fbh=1624"])
    assert len(cmd.encode()) < 608
    out = args.output.resolve()
    if out.exists():
        parser.error("use a new output directory")
    dtb = ROOT / "artifacts/daisy-dtb.bin"
    env = dict(os.environ, VINCE_NO_FBCON="1", DTB=str(dtb), PAYLOAD_CMDLINE=cmd)
    env["DAISY_ALL_CPUS"] = "1"          # all eight cores in the ADT
    subprocess.run([str(ROOT / "tools/build_daisy.sh"), str(kernel), str(out)], env=env, check=True)
    image = (out / "daisy-boot.img").read_bytes()
    shim = (out / "Image").read_bytes()
    payload = (out / "daisy.payload").read_bytes()
    tree = (out / "daisy.adt").read_bytes()
    magic, ks, ka, rs, ra, ss, sa, ta, page, hv, osv, name, bootcmd, digest, more = struct.unpack_from("<8s10I16s512s32s1024s", image)
    assert magic == b"ANDROID!" and page == 2048 and hv == 0 and ss == 0
    assert (ka, ra, sa, ta) == (0x80008000, 0x81000000, 0x80f00000, 0x80000100)
    # the shim carries the scaled console (console.c's VIG0 mode), so it differs
    # from the 0208 reference by design; the DTB append is still checked exactly
    assert image[page:page + ks] == shim + dtb.read_bytes() == (out / "Image.dtb").read_bytes()
    aligned = lambda n: (n + page - 1) // page * page
    assert image[page + aligned(ks):page + aligned(ks) + rs] == payload
    assert len(image) == page + aligned(ks) + aligned(rs)
    digest_check = hashlib.sha1()
    for block in (shim + dtb.read_bytes(), payload, b""):
        digest_check.update(block)
        digest_check.update(struct.pack("<I", len(block)))
    assert digest == digest_check.digest().ljust(32, b"\0")
    assert payload[0x48:0x48 + 608].split(b"\0")[0] == cmd.encode()
    print("PASS: kernel checksum, Metal/HID/clock markers, boot header and digest, appended DTBs, empty second (SMP: eight cores, no daisylog)")
    print("Boot arguments:", cmd)
    print("Image SHA-256:", hashlib.sha256(image).hexdigest())


if __name__ == "__main__":
    main()
