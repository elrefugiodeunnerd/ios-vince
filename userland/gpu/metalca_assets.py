#!/usr/bin/env python3
"""Assemble CoreAnimation's library from this iOS image and the CI Mac's AIR slice.

Uses pinned hakios-xnu-kernel tools/merge_metallib.py, staged beside this script.
No system shaders are stored in the source repository.
"""
import argparse
from pathlib import Path
import struct
import tempfile

import merge_metallib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, help="macOS QuartzCore AIR64 metallib")
    parser.add_argument("functions", type=Path, help="this iOS image's pipelinelib/functions")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.functions.read_bytes()
    variants = {}
    at = 0
    with tempfile.TemporaryDirectory(dir=args.output.parent) as tmp:
        part = Path(tmp) / "part.metallib"
        while (at := data.find(b"MTLB", at)) >= 0:
            if at + 24 > len(data):
                raise ValueError("truncated metallib header in functions")
            size = struct.unpack_from("<Q", data, at + 16)[0]
            if size < 88 or size > len(data) - at:
                raise ValueError("invalid metallib length in functions")
            part.write_bytes(data[at:at + size])
            lib = merge_metallib.read(str(part))
            for function in lib["funcs"]:
                name = dict(function["tags"])[b"NAME"]
                variants.setdefault(name, (function, lib["head"]))
            at += size
    expected = {name.encode() + b"\0" for name in (
        "fixed_vert_lph_gen", "fixed_vert_lph_spc", "fixed_frag_lph_cpf", "fixed_frag_lph_cph")}
    if not expected.issubset(variants):
        raise ValueError(f"missing iOS uber shaders: {expected - variants.keys()}")
    base = merge_metallib.read(str(args.base))
    replaced = set()
    functions = []
    for function in base["funcs"]:
        name = dict(function["tags"])[b"NAME"]
        if name in variants:
            function = variants[name][0]
            replaced.add(name)
        functions.append(function)
    for name, (function, _) in variants.items():
        if name not in replaced:
            functions.append(function)
    base["funcs"] = functions
    base["head"] = next(iter(variants.values()))[1]
    merge_metallib.write([base], str(args.output))
    check = merge_metallib.read(str(args.output))
    if len(check["funcs"]) != len(functions):
        raise ValueError("merged library did not round-trip")
    print(f"MetalCA: {len(functions)} functions, {len(variants)} iOS uber shaders: {args.output}")


if __name__ == "__main__":
    main()
