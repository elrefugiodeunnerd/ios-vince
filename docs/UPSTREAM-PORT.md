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

**Phone result (2026-11-02): PASS.** The rebuilt userdata booted with the
0208 image. The persistent console
(`artifacts/console-metalsmoke-a5xx-20261102.log`, 1,147,465 bytes,
SHA-256 `d84d54ea2ebd7cd6c4eac2face124ebe1910298f9cd89423f55e07c5a89900a8`)
shows all eleven GPU stages, the verified 650 MHz configuration, pool v4
and the allocator memory test. In backboardd:

```
METAL SMOKE: device open, gpu id 0x5000600
METAL SMOKE: GEM allocated and mapped, out=0xc4fa0000 constants=0xc4fa4000
METAL SMOKE: compiling AIR for chip 0x05000600
METAL SMOKE: kernel compiled: fill_id, threadgroup 1x1x1
METAL SMOKE: dispatch encoded, submitting 4 workgroups
VinceGPU: submit fence=2 bos=5 IBs=1 first=0xc4fec000 words=119
VinceGPU: completed fence=2
METAL SMOKE: out[0] = 0x00c0ffee correct
METAL SMOKE: out[1] = 0x9ef8799f correct
METAL SMOKE: out[2] = 0x3d2ff350 correct
METAL SMOKE: out[3] = 0xdb676d01 correct
METAL SMOKE: PASS: the A506 ran the a5xx dispatch and the buffer reads back
```

The AIR front end, ir3 encoding, A5xx dispatch registers, submit and fence,
and the readback are now validated on hardware. backboardd stayed up and
kept rendering (`redwood-gl` frames, touch input); there were no
backboardd or SpringBoard crashes. The only repeated failure is
`CollectionsPoster` (`extension check-in timeout`, then "too many corpses"),
which is separate from the GPU path.


### The a5xx render smoke — initial image, 2026-11-03

The render half of the smoke, opt-in by rendersmoke=1 (the injector loads
the same libmetal-smoke.dylib): one tri_flat draw over a 64x64 RGBA8
target — six vertices, the whole target covered — through render5's a5xx
sequence, then all 4096 pixels read back against the flat colour
20 40 80 ff (RGBA8Unorm, exact by UNORM conversion). The two stages are
compiled by the phone from the embedded tri_flat bitcodes.

- Userdata rebuilt with the gpu-stack from run 41532261573 (libmetal-smoke
  grew to 8.4 MiB with the linked Mesa compiler archives):
  /hdd/ios-daisy-root/ios16-root-20H392.simg, SHA-256 b11c74402a3ca943f1
  (full value in /hdd/ios-daisy-root/ios16-root-20H392.img.sha256).
- Image: artifacts/daisy-gpu-rendersmoke-0208/daisy-boot.img, 17,981,440
  bytes, SHA-256
  daecd1dbf3bb0fe5b1f55facebe3195eb6b2526df085465c8336ec26c2eda4cc
  (the proven arguments, rendersmoke=1 for the render half).
- Console lines: "RENDER SMOKE: starting", "program compiled from the
  embedded AIR bitcodes", "draw encoded, submitting", then per-row
  correctness and "RENDER SMOKE: PASS: 4096 pixels of the a5xx draw read
  back flat" — or the first failing pixel's coordinates and value for
  diagnosing rasterization, output state, or visibility of writes.

### Render smoke submits but pixels fail — 2026-11-03

Device-aware compilation (`hakios-metal` `a9fbcfc`) removed the wrong-chip
refusal. Uploading the VS driver parameters then removed the A506 shader
driver-parameter refusal; the compiled `tri_flat` regression is covered by
`3c4db75`. Userdata built from CI run `41558244267` reached:

```text
RENDER SMOKE: program compiled from the embedded AIR bitcodes
RENDER SMOKE: draw encoded, submitting
VinceGPU: submit fence=2 bos=5 IBs=1 first=0xc4feb000 words=467
VinceGPU: completed fence=2
RENDER SMOKE: pixel (0,0) is 00 00 00 00, want 20 40 80 ff
RENDER SMOKE: pixel (0,1) is 00 00 00 00, want 20 40 80 ff
RENDER SMOKE: pixel (0,2) is 00 00 00 00, want 20 40 80 ff
RENDER SMOKE: pixel (0,3) is 00 00 00 00, want 20 40 80 ff
RENDER SMOKE: FAIL: 4 rows wrong
```

Evidence: `artifacts/console-rendersmoke-dp-20261103.log`, 481,433 bytes,
SHA-256 `86cff2e216a9762ea56fc86758b02b063ba6839908ac520b3498261522cb71c5`.
The smoke stops after four failing rows, so this does not establish that
the entire target was zero. The log records GPU startup at 650 MHz and
continued Mesa rendering, without a recorded kernel panic or backboardd /
SpringBoard crash. Extension check-in timeouts affect several widgets as
well as CollectionsPoster, followed by corpse-limit messages.

Before replacing userdata, the same boot's unified logs were extracted via
`tools/pull_logs_tsk.sh` to `artifacts/logs-rendersmoke-dp-20261103/`.
`parsed/unified.jsonl` contains 249,721 records for boot UUID
`0846DBDFE2244BFE8089EA6DFAB9C8B0`. For CollectionsPoster PID 300, launchd
records `Extension is hanging on launch. Killing.` followed by
`XPC_EXIT_REASON_EXTENSION_CHECK_IN_TIMEOUT`; the corpse-limit message
follows that termination. The console records 50 CollectionsPoster timeout
exits and 34 across other widget/poster extensions. The launch/check-in
failure still needs its own diagnosis; increasing the corpse limit would
not explain the original termination.

Comparing the Metal stream with Mesa 25.0.7's `fd5_program.c`, `fd5_emit.c`,
`fd5_emit.h`, and `fd5_blend.c` produced `hakios-metal` commit `f0d25bc` on
[`a5xx-port`](https://github.com/proxer05/hakios-metal/tree/a5xx-port):

- Extract `SYSTEM_VALUE_VERTEX_ID_ZERO_BASE` when `VERTEX_ID` is absent.
  A5xx lowers the shader input to zero-base plus `first_vertex`; VFD must
  deliver that zero-base input to `r0.x`, rather than the unused `r63.x`.
- Set bit 3 of `RB_RENDER_CNTL` and `GRAS_SC_CNTL` immediately before a
  normal draw, following `fd5_emit_render_cntl`.
- Emit the screen scissor as well as the viewport and window scissors.
- Match fd5's explicit `ROP_COPY` and unused FS output-register values.

The register mismatches are established; their individual contribution to
the observed pixels requires hardware testing. In particular, matching
`ROP_COPY` alone does not prove the old value zeroed writes while
`ROP_ENABLE` was disabled. Host tests: **275 tests, 0 failed checks**.

Unified userland CI run
[`41638661933`](https://github.com/proxer05/ios-vince/actions/runs/41638661933)
passed. Its GPU artifact checksums were verified and staged under
`/hdd/ios-daisy-root/binaries`; the downloaded artifact is retained in
`/hdd/ios-daisy-root/ci-41638661933/gpu-stack`.

- `libmetal-smoke.dylib`: SHA-256
  `a22b6b797faa95f4022bccc32eedfb55d6b45ba072bb02ef59ed77ca277cf313`.
- `libhakios-metal.a`: SHA-256
  `b2a69abd012562c45c5bca8ef271e8c0ebbd8e0db96ed623048b4fc0d9e84ae2`.

Userdata was rebuilt with `/hdd/ios-daisy-root/build_root.sh`; the final
filesystem check reports `The volume iOSRoot appears to be OK`. Build log:
`/hdd/ios-daisy-root/build_root-rendersmoke-rasterstate.log`.

- `/hdd/ios-daisy-root/ios16-root-20H392.img`: SHA-256
  `f711cc9df155ad23ece8b6a2df38c6ef0f3b55bf56ac90e39bfb436a0bd200a9`.
- `/hdd/ios-daisy-root/ios16-root-20H392.simg`: SHA-256
  `62f2a578e2e0500946307cb02a7f189d7bf7a4f2887791ceecefc87cad976a27`.

All 24 sparse userdata chunks flashed successfully (895.538 seconds).
Flash log: `/hdd/ios-daisy-root/userdata-flash-rendersmoke-rasterstate.log`.
`fastboot boot artifacts/daisy-gpu-rendersmoke-0208/daisy-boot.img` then
completed its handoff successfully (0.708 seconds); the boot partition was
not flashed. The subsequent TWRP extraction confirms the pixel result below.

### Render smoke pixel validation passes — 2026-11-03

**PASS: all 4096 pixels matched `20 40 80 ff`.** The userdata and boot image
identified above ran `hakios-metal` `f0d25bc` on the A506. Console evidence:
`artifacts/console-rendersmoke-rasterstate-20261103.log`, 569,646 bytes,
SHA-256 `c5cd0484f805854378655ba69c2ba653df686563f52ae19916cf87cdedb9abb3`.

```text
METAL SMOKE: device open, gpu id 0x5000600
RENDER SMOKE: starting (tri_flat over a 64x64 RGBA8 target)
RENDER SMOKE: program compiled from the embedded AIR bitcodes
RENDER SMOKE: draw encoded, submitting
VinceGPU: submit fence=2 bos=5 IBs=1 first=0xc4feb000 words=481
VinceGPU: completed fence=2
RENDER SMOKE: PASS: 4096 pixels of the a5xx draw read back flat
```

This validates on-device AIR compilation for both stages, A5xx vertex-ID
delivery and VS driver parameters, the initial sysmem raster/output state,
submission, fence completion, and CPU pixel readback for two nonindexed
triangles covering one linear RGBA8 target. The combined fixes passed;
their individual effects were not isolated in separate hardware tests.

The same console records all eleven GPU startup stages and the verified
650 MHz configuration. Mesa UI rendering continues through frame 680 and
HID reports touch activity with zero read errors. No kernel panic,
backboardd crash, SpringBoard crash, or GPU timeout is recorded. Extension
check-in timeouts persist: 120 exits, including 88 for CollectionsPoster,
with 97 corpse-limit messages. The next Metal validation steps are textured
and varying draws, followed by the supported CoreAnimation render path.

### Full CoreAnimation Metal plug-in test — 2026-11-03

At the user's request, the next test selects the upstream CoreAnimation
Metal plug-in for backboardd. Kernel patch `0210` publishes its discovery
properties behind `metal=1`; the unified userland workflow now builds and
signs `HakiosMetal.bundle`, with the `a5xx-port` core and device-aware
render/compute pipeline compilation including function constants.

Kernel CI `41658297622` and userland CI `41659908741` passed. The 276-test
host suite is green. The image-specific CoreAnimation shader library merges
the phone's iOS uber shaders with the CI Mac's AIR64 library (161 functions).
See [the Metal UI test record](METALCA-TEST.md) for pinned sources, hashes,
packaging checks, root installation, and the hardware evidence still needed.


### The 720p fbscale display — 0211, ready to boot — 2026-11-03

The mia1 shim's fbscale trick ported: boot-args fbw=750 fbh=1624 hand iOS a
750x1624 framebuffer (the iPhone X screen at two pixels a point, 2.25 times
fewer pixels) and the MDP5's VIG0 pipe — the only one of this MDP with a
scaler — stretches it to the 1080x2280 panel, bilinear. The processor draws
less and scales nothing.

Three pieces, each against its witness:

- shim/console.c: con_switch_scaled() reprograms the VIG0 pipe from the
  splash memory (XRGB8888 at fbw x fbh, QSEED bilinear to the panel, the
  mixer's stage moved from the ABL's RGB0 to VIG0, everything flushed) and
  switches the shim console to 32-bpp words at the scaled size. The
  register sequence is the mia1 shim's, hardware-proven on this MDP (1.16).
- shim/loader.c: parses fbw/fbh from the payload cmdline, calls the switch,
  and reports the scaled geometry to XNU in the boot_args Video struct
  (750 x 1624, 32 bpp, rowBytes 3000).
- 0211-daisy-fbscale.patch (SweetFramebuffer.cpp): when the boot-args match
 *the framebuffer's own size, the logical size is the framebuffer's own —
  iOS draws at 750 x 1624 — and the iPhone X logical size and the
  scale-to-fit are skipped. The MDP does all the scaling; flushPanel and
  the 24-bpp pack never run (fPanelBpp is 4 on this path).

- Kernel: artifacts/kernel-0211/kernel.development.vince, CI 41677067423,
  SHA-256 8df1ed9af7ee5b2ae5b65fb779524644b18c8fb56ae3316bc7ade4e009d91cf3.
- Image: artifacts/daisy-gpu-fbscale-0211/daisy-boot.img, 17,981,440 bytes,
  SHA-256 9c84560d27a6261934fdc2166e332169974e954a756370ce82246c10108027b2
  (the proven arguments plus fbw=750 fbh=1624; the shim differs from the
  0208's by the scaled console, checked by the packager's DTB/digest
  asserts instead of a byte comparison).
- Userdata unchanged — the existing gpu-stack renders at whatever size the
  framebuffer service reports.

Console lines: "DISPLAY: VIG0 scaled mode" (the shim), "SWEET FB: scaled
mode: iOS draws 750x1624, the MDP VIG0 stretches to the panel" (the
kernel), then the usual rendering markers — with every redwood-gl frame
summary now at the 750x1624 geometry.
