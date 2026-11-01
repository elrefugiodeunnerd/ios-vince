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
| hakios-xnu-kernel b8d4748 | `0208-daisy-press-power.patch` | `press_power=S` taps the power key S seconds after boot and again ten seconds later (the lock and the wake with nobody at the phone), through a delayed thread call that posts the key as the physical one does; `press_power_ms` holds each press (default 150). |
| hakios-xnu-kernel 759b6205 (kernel part) | `0209-daisy-surf-wrap-optout.patch` | `surf_wrap=0` refuses the window server's picture wraps (selector 6) again, for comparing against the copy paths the refusals push CoreAnimation onto. |
| (remaining 206 commits) | — | queued; see the inventory. |

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


### 0207 early panic and fix — 2026-11-01

The first 0207 build panicked a few frames into boot: `EARLY PANIC: Mutex
... is unexpectedly owned by thread ... @lock_mtx.c:175`. The presenter
serialization in `flushPanel()` only unlocked `fPanelLock` on the
non-scaled tail; the scaled path (the one this panel always takes) returned
with the mutex held, so the second flush tried to acquire a mutex its own
thread still owned. Fixed by unlocking before the scaled path's return;
the corrected kernel is CI 41255338497 at `8ba2286` (kernel SHA-256
`185cd179a30230cd59df6ffbd3327d3d27f5b57260eb7280cdeaf16127bbfd4a`), and
the boot image is `artifacts/daisy-gpu-presenter-0207b/daisy-boot.img`
(17,981,440 bytes, SHA-256
`ac6a13ea99222ae221d288a00d75736149744b0c553fc24a65c499f45e30c4be`).
Hardware validation pending.


### 0207 hardware result — 2026-11-01

The corrected 0207 kernel ran a ~23-minute interactive session (touch log
`ms1398169`, 17,627 valid FT5446 contacts, 4 display on/off cycles) with no
panic, no crash and no pool exhaustion. Console:
`artifacts/console-gpu-presenter-0207.log`, 679,771 bytes, SHA-256
`fcc48707298d5f2164a29aba790d91b2422c1d5d6d54a96dd7344dbc93a02e29`;
unified log boots `4BBFB2E56CFC4DA8960DD08F8232F8A7` (102,779 messages) and
the earlier failed attempt's `049D9BFC159E4E4CBEA50913D912FDB8`.

- The presenter works: `SWEET FB: presenter: fb_pri=92 fb_filter=2`, and the
  settle redraw fired (`SWEET FB: settled redraw 1..4`, log capped at four)
  when the screen stood still.
- 325 frame summaries over 7,280 frames. Copy-out is solved: average 38 us,
  maximum 57 us (0204: 151-164 ms), `0 pictures copied out, 1 drawn in
  place` throughout, and the bridge's whole-session census reports 0 errors
  with 1,971 IOSurfaces as texture memory against 73 copies.
- 59 of the 325 summaries show 138-263 ms frames whose time is almost all
  `waiting` (CA/GPU), not copying: the remaining smoothness gap is in the
  render-and-present pipeline, consistent with the wrapped icons/shadows
  finding and the queued driver rework.
- 650 MHz and the memory test passed as before; `IOMFBDisplayRefresh` read
  by backboardd; reclaims advancing; zero GL errors.

The port stands at 3 of 210 upstream commits hardware-validated (0206
kernel, 0207 kernel, eagl diagnostics in the root).


The 0208/0209 kernel is CI 41349288174 (kernel SHA-256 `484522b9c2388dcc230b0fc5831f3f0c489720210a90af9201c5c2ab4d8e6529`), packaged as `artifacts/daisy-gpu-taps-0208/daisy-boot.img` (17,981,440 bytes, SHA-256 `18f993e13cd68798e6a63e951e243d3c3dc46ce8845145da534ee7c33365996d`) with the proven arguments; `press_power` and `surf_wrap` stay off unless their boot args are given. The caenv switch is in the deployed gpu-stack source (rebuilt on the next gpu-userland artifact).


### Driver port state — the a5xx-port branch, 2026-11-01

The driver port lives on the `a5xx-port` branch of
[proxer05/hakios-metal](https://github.com/proxer05/hakios-metal) (forked
from oosexgang's; the clone is
`artifacts/upstream/hakios-metal`, Mesa source at
`artifacts/upstream/mesa-src/mesa-25.0.7`). Seven commits so far:

- `a5xx.hpp`: every register Mesa's fd5 driver references for compute and
  draw on the Adreno 506 — 157 registers, 4 rnndb arrays, and the
  array-nested registers under their generated names (161 machine-checked
  constants), gathered mechanically from the fd5 sources and verified
  against Mesa 25.0.7's `a5xx.xml` by `tools/check-a5xx.py` (0 failures).
- `compute5.hpp` + `tests/compute5_test.cpp`: the fd5 launch-grid sequence
  as encoders (thread size, footprints, branchstack, constlen/instrlen,
  regids, NDRANGE, CP_EXEC_CS opcode 0x33). The host test decodes every
  packet header with pm4.hpp's reader and holds every value to fd5-derived
  literals: PASS, 40 words. The XML corrected three assumptions on the way
  (the SP_CS_CTRL bitfield layout, TWO_QUADS = 0, the a5xx CP_EXEC_CS
  opcode), which is the discipline working as intended.

Both tools read their paths from the tree; `tools/gen-a5xx.py` regenerates
the header from the fd5 sources. Next: the fake GPU's A5xx dispatch checks
under `make test`, then the IOKit transport onto VinceGPU and `render5.hpp`'s
sysmem draw. The in-process compiler still needs the AIR front-end Mesa
fork (vcvkk's `darwin-redwood-air` tree, not yet public); everything above
builds without it.
