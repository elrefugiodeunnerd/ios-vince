# Upstream port: oosexgang stack to Daisy

Daisy tracks two upstream repositories that drive an A6xx phone (Adreno
642L) with the same community kernel/userland shape. The pinned references
in [GPU-PLAN](GPU-PLAN.md) are far behind:

- `oosexgang/hakios-metal`: Daisy pinned `54a46fad`; upstream is at
  `a097abde` — **190 commits** (2026-10-27..11-01), 110k lines: a complete
  Metal plug-in and driver (AIR→NIR→ir3 compiler in-process, render with
  GMEM and hardware binning, textures, compute, tests).
- `oosexgang/hakios-xnu-kernel`: Daisy pinned `a3384e03`; upstream is at
  `7caa4576` — **20 commits**: CoreAnimation refresh-rate pacing,
  selector-6 surface work, panel-scaling filters, diagnostics.

The full commit inventory with files per commit is regenerated from fresh
checkouts in `/tmp/opencode/upstream` and kept at
`artifacts/port-inventory.txt` (210 commits).

## How the port is organised

Each upstream commit is ported as a Daisy patch, host-tested where a
harness exists, CI-built with linked-kernel markers, and documented here.
Three classes:

1. **Direct port** — userland/build/diagnostic work that is
   hardware-agnostic (build shims, fps_info, cputop, queue lifecycle).
2. **Adapt** — kernel IOKit concepts mapped onto Daisy's drivers
   (IOMFBDisplayRefresh → AppleCLCD in SweetFramebuffer.cpp; selector-6
   surface work → SweetSurface.cpp).
3. **Rework** — the Metal driver's A6xx register/PM4 layer
   (`include/hakios-metal/a6xx.hpp`, render/texture/compute encoders,
   GMEM/binning) does not exist on the Adreno 506; these commits land only
   as A5xx equivalents and are the long pole. Daisy meanwhile renders
   through Mesa freedreno, which already implements most of this for A506.

## Status

| Upstream | Daisy | What |
| --- | --- | --- |
| hakios-xnu-kernel b6c1f82, 0dedaec, f6ebf31 | `0206-daisy-fb-refresh-rate.patch` | `IOMFBDisplayRefresh` on AppleCLCD: the refresh CoreAnimation clocks frames by, in 2^-32 s units plus the MachTime step, with `fb_hz`/`fb_ca_hz` boot args. Without it CA takes a built-in panel for 60 Hz whatever the timing says; the reference phone needed the 32.32 units fix (ticks read as 26844 Hz). |
| (remaining 207 commits) | — | queued; see the inventory. |
