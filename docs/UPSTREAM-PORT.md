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
| hakios-xnu-kernel aadc757 | userland/gpu/eagl.c (repo, gpu-userland build) |  logs why a surface is copied rather than shared, for the first 40 refusals of a run (icons and glyphs: alpha first or not BGRA; nothing full-screen on the reference phone). |
| hakios-xnu-kernel cd8fdbb | `0207-daisy-fb-presenter.patch` | The presenter: panel copies leave the vsync workloop for a thread at `fb_pri` (92, BASEPRI_PREEMPT) so a slow flush cannot delay the vsync notifications nor lose the processors mid-flush; `fb_filter=2` (the default) draws nearest while frames come and redraws the last one bilinear after 40 ms still, keeping what stands still smooth (1.9 ms nearest against 3.6 ms bilinear on the reference phone). |
| (remaining 207 commits) | — | queued; see the inventory. |

Boot-tested artifacts for this port so far: kernel `artifacts/kernel-0206/kernel.development.vince` (SHA-256 `4281d20fbb9a6393444f1f30bf884b2f6ef4cd7be25d4cd64e3d9c401c77303a`, CI 41245847218 at `08a1b91`) packaged with the proven 0205 reclaim arguments as `artifacts/daisy-gpu-refresh-0206/daisy-boot.img` (17,979,392 bytes, SHA-256 `a659c2305d56196cc6a729449cdfcf0d807706079a81c0de7343d850d689186f`); the rebuilt userdata root with the macos-15 gpu-stack is `/hdd/ios-daisy-root/ios16-root-20H392.simg` (SHA-256 `b625a367c25b93fefa1cf3aefb8fa93eb66e6e2992c389ee5a0935d7e779dc84`).


## 0206 hardware result — 2026-11-01

First boot of the 0206 kernel with the rebuilt userdata root (macos-15
gpu-stack). User report: blur renders correctly, but app icons and their
shadows are not visible in the all-apps view. Console:
`artifacts/console-gpu-refresh-0206.log`, 383,261 bytes, SHA-256
`da1f148b8cb009c752031ab9fef9536545ce8b6699a6aaf9a907825a9910c22a`;
unified log boot `049D9BFC159E4E4CBEA50913D912FDB8` (158,893 messages,
zero unresolved).

- The new pacing property works on hardware: `SWEET FB: IOMFBDisplayRefresh:
  60 Hz, an interval of 71582788 (2^-32 s), 319999 ticks`, and backboardd
  reads it (`getbin AppleCLCD.IOMFBDisplayRefresh -> found`).
- The 0205 successes repeat: 66 frame summaries, 16 swaps, zero GL errors,
  zero pool exhaustion, reclaims advancing, no crashes.
- The new refusal diagnostics identify the icon path exactly: the icon-sized
  BGRA surfaces (`wrap sel 6 id 5 194x237 fmt BGRA bpr 776 size 183912
  delta 0 ro 1`) are wrapped client pictures, read-only and therefore
  ineligible for GPU import, so `surface_shared()` refuses them with
  `(the library refused)` and CA takes the copy path; the glyph masks
  (`A008`, `LA08`) and gradients (`RGhA`) are refused as not-BGRA as the
  reference phone also saw.
- The copy path renders these wrapped pictures without errors yet the icons
  and shadows do not appear: this is the known A5xx framebuffer-fetch effect
  family (CA's private icon/shadow shaders), which Mesa enables only on A6xx.
  Upstream solves it natively in its driver (`abd24c1` framebuffer fetch,
  `7a329d8` CoreAnimation's widget passes) — part of the queued 190-commit
  driver rework.
