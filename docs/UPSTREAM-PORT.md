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


Merging future upstream changes: the fork's `main` mirrors
oosexgang/hakios-metal's, and the port commits sit on top of its newest
head, so an update is

```sh
cd /hdd/ios-daisy/artifacts/upstream/hakios-metal
git fetch origin && git rebase origin/main a5xx-port
git push --force-with-lease fork a5xx-port && git push fork origin/main:main
```

The port touches only new files (`a5xx.hpp`, `compute5.hpp`,
`tools/check-a5xx.py`, `tools/gen-a5xx.py`, `tests/compute5_test.cpp`), so
rebases should stay conflict-free until the port starts modifying upstream
sources (the fake GPU checks, the transport).


### oosexgang survey — the missing parts found, 2026-11-01

All seven repositories checked. The findings, most important first:

1. **The AIR front end was never a private fork**: `hakios-xnu-kernel`
   carries it at `tools/mesa/air/` (air2nir, air_to_nir and friends) plus
   `tools/mesa/air-frontend.patch` and `air-ir3.patch`, which build it into
   stock Mesa 25.0.7 — both patches apply cleanly to the tree already
   staged under `artifacts/upstream/mesa-src/`. `tools/air/mk-host.sh` is
   the complete host recipe (it fetches the pinned tarball, applies the
   patches, works around the glibc 2.43 `<threads.h>` clash, and builds
   `air2nir`, `ir3_compiler` and `air_run`); it ran unmodified on this
   machine, producing the build at
   `hakios-xnu-kernel/build/mesa-host/build/`.
2. **The full hakios-metal test suite now passes WITH the compiler**:
   `make test MESA=1` against that build — 268 tests, 0 failed checks,
   including the fixture comparisons that compile `tools/kernels/*.ll`
   on this machine. The one host wrinkle: the AIR reader reads LLVM 14/15
   bitcode, and this machine's `llvm-as` is 23 — `tools/llvm-as-15.py`
   (llvmlite) builds the fixtures, wired in with `LLVM_AS=`. The compiler
   blocker is closed; the phone's in-process compilation of Apple's own
   `.metallib` shaders is now a build flag, not a missing piece.
3. **The device-repository architecture**: `hakios-sm7325` is the template
   — device.conf, configs/, its UEFI platform, firmware-pull scripts, and
   the SoC layer. The README says plainly: a port to another Qualcomm phone
   is another directory like it. A `hakios-msm8953` device repo is how a
   Daisy port would be carried upstream-shaped, if wanted.
4. **The UEFI boot path**: `hakios-uefi` (a Mu-Silicium port) plus each
   device's `uefi/` platform and prebuilt `BootShim.bin` — upstream's newer
   boot path. Daisy still boots through the Android shim; this is a future
   consideration, not a gap.
5. `hackios-xnu-userland` is a pointer to the kernel repo (userland moved
   into it — our port base is correct); `hackios-web` is documentation;
   `hakios-mt6768` is a MediaTek/Mali port, not Adreno.
6. **Kernel drift**: `hakios-xnu-kernel` main is `5c819b8` (HakiosMetalCA:
   the window server's compiled stages cached in /var/tmp) — 31 commits
   past the old pin, and the metal repo a few past the branch base too.
   The next sync picks those up by the documented rebase procedure.

The toolchain state on this machine: the Mesa+AIR host build lives at
`hakios-xnu-kernel/build/mesa-host/` (meson in `~/.local/bin`, mako/pyyaml
in `~/.local`), and `hakios-metal` tests pass against it with
`make test MESA=1 MESA_SRC=... MESA_BUILD=... LLVM_AS='python3
tools/llvm-as-15.py'`.


### The a5xx Metal smoke test image — 2026-11-02

The first phone test of the a5xx dispatch path end to end: the 0208 kernel
(650 MHz, pool v4, presenter, IOMFBDisplayRefresh), a userdata root carrying
the macos-15 gpu-stack plus libmetal-smoke.dylib, and metalsmoke=1 in the
boot args. The smoke (userland/gpu/metal_smoke.cpp, injected into
backboardd with the GPU stack) opens the GPU service through the ported
transport, prints the chip id VinceGPU reports, compiles fill_id ON the
phone with its own AIR front end (the LLVM 15 bitcode embedded in the
dylib), dispatches it over a grid of 4 through the a5xx path, waits and
reads the buffer back against gid * 0x9E3779B1 + 0x00C0FFEE.

- Kernel: artifacts/kernel-0208/kernel.development.vince (SHA-256
  484522b9c2388dcc230b0fc5831f3f0c489720210a90af9201c5c2ab4d8e6529).
- Image: artifacts/daisy-gpu-metalsmoke-0208/daisy-boot.img, 17,981,440
  bytes, SHA-256
  4ab5d5cf2d3631c05c18048cee7a5334865d25eba59d1567733bccdb724e7935
  (the proven arguments plus metalsmoke=1).
- Userdata: /hdd/ios-daisy-root/ios16-root-20H392.simg (sparse, SHA-256
  254f7555e4d0652368f5332585f2893ebcda1f8414e58cb26d8d860f3f716b1f), built
  by build_root.sh with the gpu-stack 41367936111 artifacts
  (libhakios-metal.a from the a5xx-port fork, libmetal-smoke.dylib linked
  against it; the build log records the install into /daisy/lib).
- What to look for in the console: "METAL SMOKE: device open, gpu id ..."
  (the chip id the transport ABI reports), "kernel compiled", then
  "out[0..3] = ... correct" and "METAL SMOKE: PASS" — or the first failing
  line, which names the layer (transport, compiler, encode or memory).

That run crash-looped: the dylib's constructor ran before command.cpp's
and texture.cpp's static initializers, so every `newBuffer()` reached an
unbuilt map and `metal_smoke` SIGSEGV'd 79 times. `daisy_inject.c` now
calls an exported `daisy_metal_smoke()` after `dlopen` returns, and the
smoke refuses anything but an A506, initializes both GEM buffers, uses a
1x1x1 group and waits on the dispatch fence.

### The render5 host slice and the rebuilt smoke userdata — 2026-11-02

The Metal tree (`proxer05/hakios-metal`, branch `a5xx-port`, commit
`926f103`) now carries the initial A5xx draw slice:

- `render5.cpp`: a sysmem pass for one linear RGBA8/BGRA8 target with a
  nonindexed triangle list, one instance, no depth/MSAA/blend/multiple
  targets. It serves pushed constants and sampled VS/FS textures, and
  implements framebuffer fetch as a flushed texture read (one triangle per
  fetching draw). Unsupported state is refused before anything reaches the
  stream.
- `texture5.hpp`/`texture.cpp`: fd5-formatted texture and sampler tables and
  single-level linear colour descriptors.
- `compute.cpp`/`compiler.cpp`: A5xx dispatch encoding fixes and per-chip
  compiler instances, so AIR is compiled for the chip the transport reports
  instead of the A6xx default.
- `tools/gen-a5xx.py` also finds registers fd5 names only through a field
  macro prefix (`GRAS_SC_DEST_MSAA_CNTL`, `TPL1_TP_DEST_MSAA_CNTL`), which
  completes render5's restore list.

Host: `make test MESA=1 ...` reports **273 tests, 0 failed**, including the
new `render5_sysmem_draw_words`, `render5_refusals_leave_the_stream_empty`
and `texture5` tests.

The userland workflows are now one `userland` workflow with a `helpers` job
and a `gpu-stack` job (run 41471285426, both green); `gpu-userland.yml` is
gone. The `gpu-stack` job links the iOS Mesa archives into
`libmetal-smoke.dylib`, so the on-device AIR compiler is really present:
`nm` shows `_air_to_nir` and `_ir3_compile_shader_nir`, and the dylib grew
from 707 KiB to 8.4 MiB.

- `libhakios-metal.a` SHA-256
  a6839c5e516215b889b74f568ced0259bd1e34425faede57300563846d2a93f4
- `libmetal-smoke.dylib` SHA-256
  c4021eaf3b08605df7d1646a88c93856c1bfd9e7a98b0410cad48cab6874b28a
- Userdata rebuilt by `/hdd/ios-daisy-root/build_root.sh` and staged with
  those artifacts: `ios16-root-20H392.img` SHA-256
  cfdd6a1471f48b88cf8ae7409aeb1a375e78b3aad20c7a7571a61a6235143205,
  sparse `ios16-root-20H392.simg` SHA-256
  5feb423bd7b6a84b7b760b8507e7ccaa18133b7332e05eb5f8136933cf0966e8.
- The 0208 boot image is unchanged (same SHA-256 as above); it already
  carries `metalsmoke=1`.

Next: flash the rebuilt userdata and `fastboot boot` the 0208 image. The
phone must reach "kernel compiled", a completed dispatch and fence, the
four correct words and "METAL SMOKE: PASS". Nothing below that line is
validated hardware behaviour yet.
