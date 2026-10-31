#!/usr/bin/env python3
"""Run the patched startup clock code against an ordered, fault-injected MMIO peer."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent


def main():
    with tempfile.TemporaryDirectory(prefix="daisy-clock-", dir="/tmp/opencode") as tmp:
        work = Path(tmp)
        subprocess.run(["git", "init", "-q", str(work)], check=True)
        for number in ("0181", "0195", "0203"):
            patch, = (ROOT / "xnu/patches").glob(number + "-*.patch")
            subprocess.run(["git", "-C", str(work), "apply",
                            "--include=bsd/dev/vince_adreno.c",
                            "--include=bsd/dev/daisy_gpu_clock.h", str(patch)], check=True)
        parent = (work / "bsd/dev/vince_adreno.c").read_text()
        defines = "\n".join(re.findall(r"^#define (?:GCC_|GDSC_)\w+[^\n]*", parent, re.M))
        c = work / "test.c"
        c.write_text("""#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
static vm_offset_t gcc_va = 1;
static uint32_t last_step = 11, skip_steps;
static uint32_t reg_read(vm_offset_t, uint32_t);
static void reg_write(vm_offset_t, uint32_t, uint32_t);
static void delay(unsigned);
static bool PE_parse_boot_argn(const char *, void *, unsigned);
static vm_offset_t ml_io_map(vm_offset_t, unsigned);
""" + defines + '\n#include "bsd/dev/daisy_gpu_clock.h"\n' +
                     (TESTS / "daisy_gpu_clock.c").read_text())
        binary = work / "test"
        subprocess.run([os.environ.get("CC", "clang"), "-std=gnu11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        str(c), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        print(result.stdout.splitlines()[-1])


if __name__ == "__main__":
    main()
