# Daisy Adreno 506 bring-up

## Current status — 2026-11-03

End-of-day resume notes and persistent `/tmp` copies:
[`GPU-HANDOFF-2026-10-31.md`](GPU-HANDOFF-2026-10-31.md).

The user considers touch sufficient for navigation and has asked to focus on
GPU acceleration. Preserve `artifacts/daisy-touch-hid-0194-smp/daisy-boot.img`
as the navigable baseline (SHA-256
`a6678e5576c3376c4424704c10d65aab7fbe13c7aca7546fe49cea12a9c75fc1`).

**The full Adreno 506 bring-up now passes on Daisy.** The `adreno=11` image
booted to SpringBoard, and its persistent log records all eleven steps
succeeding, including the ZAP/PIL 13 path and a post-ZAP command-processor
write. `VinceGPU` registered, touch and keys worked, and there was no panic.
See [Hardware result — 2026-10-30](#hardware-result--2026-10-30) for the
evidence and the revised remaining path.

**0195 also passes the allocator-backed GPU memory test.** The user
confirmed a homescreen boot. The recovered log shows an indirect command
buffer crossing a page boundary, a write to the last word of a 16 KiB
allocation at `0xa3ff3000`, and completed cache-flush fence 1. See
[Kernel memory patch — 0195](#kernel-memory-patch--0195).

The earlier `adreno=6` image produced no persistent log, but the user
confirmed that image also reached the UI; the missing file was a
logger/filesystem problem, not a step-6 abort. The `adreno=5` image also
reached the lockscreen.

The imported `0181-vince-adreno-506-bringup.patch` contains the hardware
startup steps and the `VinceGPU` IOKit command-submission service. Its
`gpu=1` selector requests all eleven steps; `adreno=N` stops after step N.
Both are opt-in. The full GPU userland stack is deployed and verified.
**0198 produces visible GPU-rendered frames.** The user reports working blur
but severe lag. The recovered console shows `FD506 / OpenGL ES 3.1`, GEM
submissions, BGRA framebuffer swaps and one Mesa context, with no recorded
backboardd crash loop. The copy path takes about 160–220 ms per readback;
111 failed 12 MiB contiguous GEM allocations cause additional multi-second
stalls and GL errors. Shared-surface rendering remains unvalidated. See
[0198 rendering result](#0198-rendering-result--2026-10-31).

**The A5xx draw path now has a hardware-validated flat-draw slice.** The `a5xx-port`
Metal tree adds `render5.cpp` (one linear RGBA8/BGRA8 sysmem target,
nonindexed triangle lists, pushed constants and sampled textures, flushed
framebuffer fetch), `texture5.hpp` fd5 descriptors and tables, per-chip AIR
compilation, and the A5xx dispatch fixes. The host suite is 275/275 green.
The smoke crash loop was a static-initialization order bug, now fixed by
exporting `daisy_metal_smoke()` and calling it after `dlopen`. The rebuilt
userdata has been tested with the 0208 image. See
[the render5 section](UPSTREAM-PORT.md#the-render5-host-slice-and-the-rebuilt-smoke-userdata--2026-11-02).

**On the phone the smoke passes end to end (2026-11-02).** The device opened
`VinceGPU` as chip `0x05000600`, compiled `fill_id` with its own AIR front
end, encoded and submitted 119 words in 5 buffers, completed fence 2, and
read back all four correct words. backboardd remained alive and rendering.
This validates transport, GEM, on-device compilation, A5xx dispatch,
submit/fence and memory order.

**The render smoke passes all 4096 pixels on hardware (2026-11-03).**
`hakios-metal` `f0d25bc`, built by unified userland CI run `41638661933`,
compiled both AIR stages on the A506, submitted the 481-word draw, completed
fence 2, and read back `20 40 80 ff` at every pixel of the 64x64 target.
This validates the initial flat-colour, nonindexed sysmem draw end to end.
The earlier zero-pixel failure was followed by fixes to the lowered
vertex-ID register, normal-draw raster control, screen scissor, and output
state; all 275 host tests also pass. The console records continued Mesa UI
rendering and touch activity, without a kernel panic or backboardd /
SpringBoard crash. Widget/poster extension check-in timeouts persist. See
[hardware evidence](UPSTREAM-PORT.md#render-smoke-pixel-validation-passes--2026-11-03).

**The next boot tests the full CoreAnimation Metal plug-in.** Kernel `0210`
adds opt-in Metal device discovery; unified userland builds the upstream CA
plug-in over the device-aware A5xx core. See [Metal UI test](METALCA-TEST.md)
for build inputs, the prepared boot image, and required hardware evidence.

## Recovery evidence

Temporarily booted TWRP on serial `6790a29e0305` after the user returned to
fastboot. Saved evidence under `artifacts/daisy-gpu-identification/`, with
`SHA256SUMS` covering the collected files:

- `kgsl.txt`: **Adreno506v1**, sampled frequency 320 MHz, seven advertised
  levels: 650, 560, 510, 400, 320, 216 and 133.33 MHz; reset count zero.
  These are Linux recovery observations, not XNU measurements.
- `device-tree.dtb`: SHA-256
  `8dbb42ad1be1ff4505108813f825edbf0c8b739a5b27cc1bc1590536e3e79afc`.
- `iomem.txt`, `dmesg.txt`, `cmdline.txt`, `mounts.txt`: live recovery layout
  and initialization evidence. `dmesg.txt` retains recovery's ANSI escapes.
- `firmware/` and `firmware-comparison.txt`: GPU firmware extracted from
  `vendor_a`, mounted **read-only with `noload`** at a temporary recovery path.
  `modem_a` was also inspected read-only. Both were unmounted before reboot.
  The HFS iOS root was not mounted during this inspection.

### GPU and memory layout

| Resource | Daisy evidence | Imported driver |
| --- | --- | --- |
| GPU registers | `0x01c00000`, size `0x40000` | Matches |
| GPU SMMU | `0x01c40000`, size `0x10000` | Assumes TZ leaves bypass; no page-table setup |
| PAS/ZAP subsystem | DT `qcom,pas-id=13`, `a506_zap` | Matches |
| GPU RAM scratch | `0x47800000..0x479fffff` lies in Linux System RAM and below XNU's RAM | Fixed 2 MiB region |
| ZAP reservation | DT uses an 8 MiB reusable pool, 4 MiB alignment, allocation range `0x80000000..0x8fffffff`; recovery allocates it at `0x8f800000` | Fixed `0x95000000`, size `0x2000` |

The recovery CMA allocation is not a fixed bootloader reservation. Neither
its address nor the inherited `0x95000000` should be described as an already
validated XNU ZAP destination. Establish a Daisy-owned region and check its
relationship to the bootloader, payload, framebuffer, XNU RAM and other
firmware before the ZAP execution test. The current `adreno=6` image stops
before GPU RAM/firmware setup.

XNU keeps `PHYS_BASE=0xa0000000`, `MEM_SIZE=0x57f00000`. The Android image
loads the shim at `0x80008000`, payload at `0x81000000`; the splash reservation
is `0x90001000..0x913fffff`. The collected recovery layout alone does not
prove GPU DMA accessibility or the claimed SMMU bypass state.

### Firmware comparison

| File | Daisy bytes | SHA-256 | Matches embedded 0181 |
| --- | ---: | --- | --- |
| `a506_zap.mdt` | 6,684 | `1498340d7474c4c0ae409452466c29f750e2e3a44afed606f4e8a79156b688dc` | Yes |
| `a506_zap.b02` | 5,188 | `fe8dead286927c662da0769589c739f914c130271429701606bd7662e91930fd` | Yes |
| `a530_pm4.fw` | 19,652 | `a02c9be11787472d4ab0173ed8552afbde1f31b1e81bcd87adf1fcea97b6115f` | Yes |
| `a530_pfp.fw` | 17,108 | `2a51c25b1f7299cb84c3685d3d1fb88bf0504494456345b2dfde32816e13bc8e` | **No** |

The embedded PFP is 16,820 bytes, SHA-256
`cc4f2b1cd6b3a9401592e2498ec43b87ba926a23848fa231f417ab08df9c47ba`.
The mismatch is established; it has not been shown to cause an XNU failure.
The first GPU diagnostic does not load either PFP image. Keep the native
PFP available for a separately identified command-execution build.

The native ZAP ELF has one relocatable `PT_LOAD` segment: physical address
`0x5000`, file/memory size `0x1444`, flags `0x08000007`. Metadata and the
segment match the embedded blobs. `b00`/`b01` are preserved alongside them.

## First test: steps 1–6 on the existing 0194 kernel (historical)

> This was the first step-6 probe. The full eleven-step result is in
> [Hardware result — 2026-10-30](#hardware-result--2026-10-30).

This reuses CI [40964379162](https://github.com/proxer05/ios-vince/actions/runs/40964379162)
and `MACHINE_CONFIG=VINCE`; no kernel changes were needed for the first test.
It retains filtered touch and the no-fbcon artwork ADT, uses `cpus=1` for
the persistent logger, and adds only `adreno=6` to the single-core HID image's
command line. No raw touch capture or delayed touch probes are selected.

```sh
VINCE_NO_FBCON=1 DTB="$PWD/artifacts/daisy-dtb.bin" \
PAYLOAD_CMDLINE='debug=0xc serial=3 cpus=1 keepsyms=1 io=0x3f -noprogress wdt=-1 wdt_test=no_user_checkin rd=mmcroot0 mmcrw=1 rootrw=1 unentitled_ios_sim_launch=1 launchd_unsecure_cache=1 ignition_level=1 vincefix=bluetool vincetouch=0 vincekeys=1 daisytouch=1 daisytouch_gpio=1 daisytouch_power=1 startup_debug=0 -disable_kprintf daisylog=1 daisytouch_busywait=1 daisytouch_hid=1 adreno=6' \
tools/build_daisy.sh artifacts/kernel-0194/kernel.development.vince artifacts/daisy-gpu-regs-0194
fastboot -s 6790a29e0305 boot artifacts/daisy-gpu-regs-0194/daisy-boot.img
```

- Image: 17,944,576 bytes; SHA-256
  `a4a14e9aa41ceab14b6682635066d185a0087a2c1778ca69f9f2692680a2338e`.
- Kernel SHA-256:
  `010d54d08824cef3e5d28ae40ba553b36ae73469d319a26c1df4f024e8d71212`.
- Build log: `artifacts/daisy-gpu-identification/build-regs-image.log`.
- Verified Android header/hash, appended Daisy DTBs, empty `second`, full
  payload/kernel segments, ADT against the no-fbcon baseline, RAM bounds,
  command line, GPU service and touch/logger linked markers.
- Temporary boot handoff succeeded in **0.541 s**, after `product: daisy`.

Steps 1/2 read GCC and fuses; 3/4 power CX/GX; 5 programs the initial 100 MHz
clock selection and branch clocks; 6 calls `restore_sec_cfg(18,0)` and reads
GPU registers. The probe's stated expected VBIF version is `0x200d0000`.
This stage does not start `VinceGPU` or improve the rendering path.

When the phone returns to fastboot, temporarily boot recovery and extract
before another logging-enabled XNU boot:

```sh
fastboot -s 6790a29e0305 boot /smb/twrp-3.7.1_12-daisy-20260424-1114.img
adb -s 6790a29e0305 wait-for-recovery
python3 tools/pull_daisy_console.py artifacts/console-gpu-regs-0194.log --serial 6790a29e0305
```

Inspect every `adreno:`/`VINCE ADRENO:` result, clock readbacks, SCM return
and answer, VBIF version, subsequent boot progress and logger errors. The
current probe treats a completed register read as success, so the printed
`SUCCESS` lines alone are insufficient. Also, its step-6 `INT0` label reads
byte offset **`0xf8` instead of `0x1384`**; do not interpret that field as
the interrupt status. The other step-6 offsets match the pinned vendor header.
Later ring diagnostics use the correct interrupt-status dword `0x4e1`.

The GPU initializer is synchronous inside `vince_input_start()`. An early
abort may precede the filesystem logger; if no fresh log survives, move the
diagnostic behind a delayed logged worker for the next build.

## Remaining path to acceleration (2026-10-29, historical)

> Superseded by the 2026-10-30 result and revised path below.

1. **Register access:** collect the current hardware result. Correct the
   step-6 interrupt offset and make clock/SCM/readback failures explicit
   before relying on the startup gate for command execution.
2. **Command execution:** validate the chosen scratch/ZAP memory and SMMU
   state, then stage microcode/ring, `CP_ME_INIT`, scratch writes and ZAP
   authentication. Require the expected scratch values and consumed ring.
3. **Usable userspace buffers:** validate `VinceGPU` with multi-page buffers,
   repeated submissions, completion and close. Source inspection found that
   `gemNew()` uses the first physical address as a linear GPU address without
   requesting physical contiguity or establishing SMMU mappings. Buffer
   close releases memory immediately even with in-flight work, and the
   fence scratch word at `GPU_SCRATCH_OFF+0x20` is not initialized by step 8.
   Resolve these before connecting a renderer. Rendering completion also
   needs proper GPU/cache synchronization beyond a CP memory-write fence.
4. **Actual rendering:** integrate an Adreno-506-capable userspace renderer,
   shader compiler, IOSurface sharing and the CoreAnimation presentation
   path. The GPU userland stack has been imported from
   [`MakrSas/ios-topaz`](https://github.com/MakrSas/ios-topaz) (`userland/gpu/`
   and `userland/mesa/`), providing:
   - `userland/gpu/bbd_wrap.c`: `/usr/libexec/backboardd` wrapper setting `DYLD_INSERT_LIBRARIES`.
   - `userland/gpu/daisy_inject.c`: injector overriding `containermanagerd` personas, installing backtrace signal handlers, and hooking `CA::OGL::GLESContext`.
   - `userland/gpu/eagl.c` + `gl_osmesa.c`: `EAGLContext` swizzling driving Mesa freedreno/softpipe via off-screen OSMesa, binding IOSurfaces zero-copy to GL textures.
   - `userland/mesa/`: Mesa 25.0.7 build recipe (`mk-mesa-mac.sh`), Darwin patches, and `redwood-drm` (translating Linux DRM requests into IOKit user client calls against `DaisyGPU`/`VinceGPU`/`RedwoodGPU`).
   - `userland/gpu/mkeagl.py` + `eagl_slots.txt`: Macro context function dispatch table generator.
   See [`userland/README.md`](../userland/README.md) for architecture, build steps and deployment.
   `AppleCLCD` currently packs/copies the displayed surface into the 24-bpp panel buffer on the CPU.
5. **Interactive validation:** return to the eight-core, logging-disabled
   touch baseline with the validated GPU path enabled; verify real rendered
   output, navigation and measured frame timing.

### Pinned references

- Xiaomi vendor kernel `daisy-o-oss`, commit
  `6421aa5cdc56e9ef7c6329e0a05164503e1513ef`,
  [`drivers/gpu/msm/a5xx_reg.h`](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/6421aa5cdc56e9ef7c6329e0a05164503e1513ef/drivers/gpu/msm/a5xx_reg.h).
  Local copy: `artifacts/daisy-gpu-identification/a5xx_reg.h`.
- Imported GPU code: `xnu/patches/0181-vince-adreno-506-bringup.patch`, through
  upstream `401873c2facdbb618c87ccc93ff479472884269a`.
- [`ios-topaz`](https://github.com/MakrSas/ios-topaz) (`/hdd/ios-topaz`):
  provenance for the Adreno/Mesa freedreno Darwin userland stack (`userland/gpu/`,
  `userland/mesa/`).
- [`hakios-metal`](https://github.com/oosexgang/hakios-metal/tree/54a46fadec5bd9684ebd77f3a0834c84b5eb01a6),
  commit `54a46fadec5bd9684ebd77f3a0834c84b5eb01a6`: useful command-transport
  and blit reference. Its README explicitly says shaders and CoreAnimation
  through Metal are unfinished; it is not a ready-made SpringBoard renderer.
  The reported hardware checks are on Adreno 642L, not this Adreno 506.

## Hardware result — 2026-10-30

Image: `artifacts/daisy-gpu-regs11-0194/daisy-boot.img` (17,944,576 bytes,
SHA-256 `7f383a4f4de91e83ae1c4b6662f45c1eb6ada5764fb28cd0b65ea83290956968`),
built from kernel 0194 with `adreno=11`, `cpus=1`, `daisylog=1`, and the
filtered touch/HID path. Log:
`artifacts/console-gpu-regs11-0194.log` (312,553 bytes, SHA-256
`2e61319047781b6e796825b96aa92f41f582c85aa54356a8f6272cc74edf7bd7`),
extracted from `/private/var/log/daisy-console.log` with
`tools/pull_daisy_console.py`.

The relevant lines are:

```text
VINCE ADRENO 506: starting steps 1 to 11 (skip mask 0, pause 0 ms)
adreno: step 6: restore_sec_cfg(18, 0) -> ret=0 ans=0
adreno: step 6: RBBM_AHB_CNTL0=0x1 RBBM_STATUS=0x1 STATUS3=0 INT0=0 VBIF_VER=0x200d0000
adreno: step 6: GPU register space readable (VBIF_VERSION should be 0x200d0000)
adreno: step 8: ME running, RBBM_STATUS=0x1 INT0=0 CP_HW_FAULT=0
adreno: step 9: CP_MEM_WRITE -> 0xc0ffee01 (the GPU executed its first command), RPTR=13
adreno: step 10: secure ptbl size ret=0 size=0x21000
adreno: step 10: secure ptbl init at 0x47900000 ret=0
adreno: step 10: init_image ret=0
adreno: step 10: mem_setup(0x95000000, 0x2000) ret=0
adreno: step 10: auth_and_reset ret=0 ans=0
adreno: step 10: zap loaded, GPU out of secure mode
adreno: step 11: post-zap CP_MEM_WRITE -> 0xc0ffee02; RBBM_STATUS=0x1 INT0=0 RPTR=19
adreno: step 11: Adreno 506 command processor is up (ring at PA 0x47820000)
VinceGPU: service started and registered
VINCE ADRENO: step 11 SUCCESS
VINCE ADRENO 506: bringup sequence finished
```

The boot then reached SpringBoard; the log records touch/button activity
(`VINCE INPUT`, `SWEET HID`, `DAISY HID`) and no panic. This validates the
`0x47800000` GPU scratch, the `0x95000000` ZAP region, TZ restore, PIL 13,
and basic command execution on Daisy. The embedded vince PFP (16,820 bytes)
was sufficient for `CP_ME_INIT`, `CP_SET_SECURE_MODE`, and the scratch
writes; the native Daisy PFP remains untested for full draws.

The step-6 `INT0` label still reads the wrong byte offset (`0xf8` instead of
`0x1384`), so that field is not the interrupt status; the other step-6
offsets match the vendor header. It did not affect the result.

### Userland state

`/daisy/lib` contains `libdaisy-inject.dylib` and `libtopaz-inject.dylib`,
but **not** `libdaisy-gl.dylib` or `libOSMesa.8.dylib`. The log shows
`/usr/libexec/real/backboardd`, so the wrapper is installed and the real
window server is running. No `redwood-gl`/freedreno activity appears, and
`IOAcceleratorES` is absent, so backboardd repeatedly matches it (topaz
patch 0138 adds an empty stub to avoid the startup delay).

### Revised remaining path

1. **Kernel memory/interface for Mesa.** Allocate GEM buffers physically
   contiguous below 4 GiB (the SMMU is in pass-through after ZAP), fix the
   full 32 KiB ring clear (indices already count 8,192 dwords), initialize the fence scratch word,
   add explicit cache maintenance for CPU-written buffers, and return
   `MSM_INFO_GET_FLAGS`. Then add selector `0x7e` (`surfaceBuffer`) plus
   `SweetSurface` contiguous surfaces and client-memory mapping for
   zero-copy IOSurfaces.
2. **Build and deploy the userland stack.** Build Mesa 25.0.7
   (freedreno + softpipe), `eagl_gen.h`, `libdaisy-gl.dylib`, and
   `libOSMesa.8.dylib`; deploy them to `/daisy/lib`. Test first with
   `FD_MESA_DEBUG=sysmem`/softpipe, then with freedreno.
3. **CoreAnimation presentation.** Implement IOSurface sharing, cache
   coherency, the surface drawn/fence protocol, the `IOAcceleratorES` stub,
   and the backboardd jetsam exemption.
4. **Firmware.** Keep the embedded PFP for the validated
   `CP_ME_INIT`/ZAP path; test the native Daisy PFP in a separately
   identified build for full draws.
5. **Interactive validation.** Return to the eight-core, logging-disabled
   touch baseline with the validated GPU path enabled; verify rendered
   output, navigation and frame timing.

The `gpu-userland` workflow
(`.github/workflows/gpu-userland.yml`) builds the Mesa and eagl stack on
macOS and uploads the `gpu-stack` artifact for deployment.

## Kernel memory patch — 0195

`0195-daisy-gpu-contiguous-gem.patch` adds the first Mesa-facing memory
changes. The VINCE kernel build passed in
[run 41096208142](https://github.com/proxer05/ios-vince/actions/runs/41096208142)
from commit `5252f1a`. The opt-in memory test passed on hardware:

- GEM allocations are physically contiguous, WC, and below 4 GiB. The
  driver checks the complete physical segment with `kIOMemoryMapperNone`,
  bounds sizes before rounding, and publishes handles under the GPU lock.
- The full 32 KiB ring is cleared, and fence scratch starts at zero.
  **Correction to the initial audit:** `rb_wptr` and `CP_RB_RPTR` count
  dwords; their original 8,192-entry wrap was already correct.
- CPU buffers receive real A53 PoC cache maintenance and store barriers.
  Completion uses A5xx `CACHE_FLUSH_TS`, with per-buffer fences for CPU
  access and release. Diagnostic waits are bounded to two seconds; memory
  still used by a stalled GPU is retained on client teardown until reboot.
- `MSM_INFO_GET_FLAGS` reports the actual WC flags; `SET_NAME` is accepted.
  VA parameter IDs are corrected to `0x0e`/`0x0f`, and the highest bank bit
  is reported as 14. Step 6 now reads interrupt status at byte `0x1384`.
- Submissions validate buffer handles, bounds, request sizes, and unsupported
  synchronization modes. IB targets are not executed twice. Up to 64
  command buffers are accepted per submission.

The opt-in `daisygpu_test=1` test runs after step 11, before registration.
It allocates four pages using the GEM allocator, places an indirect command
buffer across a page boundary, and asks the GPU to write `0xc0ffee03` to the
allocation's last word. Success requires both the value and a completed
cache-flush fence. Look for `VinceGPU: memory test ... PASS`. On failure the
service is not registered. This test needs `adreno=11` or `gpu=1`; it is
disabled by default. Initialization still precedes the filesystem logger.

The first 0195 image retains the reference `adreno=11`, single-core,
logging-enabled diagnostic arguments and adds `daisygpu_test=1`, using the
embedded PFP. It was sent successfully with `fastboot boot` on 2026-10-30.
The user confirmed it reached the homescreen, then returned to fastboot.

- Kernel: `artifacts/kernel-0195/kernel.development.vince`, SHA-256
  `d59fc5912a63405093e047439d85cf482ad5ccb0bf0b819ef5c84a9a730831da`.
- Image: `artifacts/daisy-gpu-memory-0195/daisy-boot.img`, 17,944,576 bytes,
  SHA-256 `b8217743a107a23b4af805d57cfdb641a633d5c6da5a00a5e7ef12698ea993e2`.
- Packaging verified: shim and appended Daisy DTBs match the 0194 reference;
  `second` is empty. ADT properties match except for a fresh random seed.

The log was collected after temporarily booting the requested TWRP image:

```sh
fastboot -s 6790a29e0305 boot /smb/twrp-3.7.1_12-daisy-20260715-2011.img
adb -s 6790a29e0305 wait-for-recovery
python3 tools/pull_daisy_console.py artifacts/console-gpu-memory-0195.log --serial 6790a29e0305
```

Log: `artifacts/console-gpu-memory-0195.log`, 310,192 bytes, SHA-256
`922f5b978d4c8f3157fff8072b3158542b49c307b7ee392d8eecf6d92d513420`.
It contains all eleven startup successes and:

```text
adreno: step 8: cleared 32 KiB ring and initialized fence scratch
VinceGPU: memory test PA=0xa3ff3000 bytes=16384 fence=1 done=1 value=0xc0ffee03 PASS
VinceGPU: service started and registered
VinceGPU: Daisy GEM v1: contiguous DMA32, WC, cache-flush fences
```

This validates command fetch and a GPU write outside the startup carveout,
through the allocator-backed physical span, plus cache-flush fence completion
with the embedded PFP. No panic appears in the recovered log. Userland GEM
ioctls, full draws, and presentation remain untested. The next integration is
selector `0x7e`, contiguous IOSurfaces, and deployed Mesa rendering tests.

## Loader and IOSurface port — 0196/0197

The 2026-10-30 userdata boot still selected CoreAnimation's software renderer.
Its recovered logs contain `amfi_interface_query_bootarg_state returned error
Policy not found` and no injector/Mesa startup. Recovery extraction verified
that `/daisy/lib/libdaisy-inject.dylib`, `libdaisy-gl.dylib`, and
`libOSMesa.8.dylib` on the phone match the `gpu-stack-5a9a09f` CI artifacts
byte for byte. Daisy's MAC policy was named `SWEET AMFI` and did not answer
dyld's policy request, which prevents the intended library insertion.
The installed `/usr/libexec/backboardd` was also verified against the CI
wrapper (catalog ID 321391, SHA-256
`9b9875452ccbdae82271e70c7925d9c3048b4a35e0a25043d7a1cf12110f98d5`);
`/usr/libexec/real/backboardd` is present. Direct catalog lookup was needed
because SleuthKit fails while enumerating an unrelated entry in `libexec`.

Cross-references checked against fresh upstream checkouts on 2026-10-31:

- [hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel/tree/a3384e031eb1e605cf37adaefc19991f04f6274b):
  `xnu/files/bsd/kern/kern_minimac.c` (AMFI),
  `drivers/gpu/hakios.gpu.cpp` (surface import, ownership, fences),
  `xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp` (accelerator discovery),
  and `tools/mesa/freedreno-darwin.patch` (identical to Daisy's integration
  after bridge naming is normalized).
- [hakios-metal](https://github.com/oosexgang/hakios-metal/tree/54a46fadec5bd9684ebd77f3a0834c84b5eb01a6):
  `include/hakios-metal/gpu.hpp`, `src/gpu.cpp`, and `src/iokit_transport.cpp`
  confirm the msm request layouts, WC mappings and continuous-clock deadlines.
  Its current device probe covers command-processor copies/fills and fences
  on A6xx; its shader path is unfinished. Metal plugin registration is not
  part of this Mesa/EAGL port.

`0196-daisy-amfi-dyld-gpu-discovery.patch` registers the exact policy name
`AMFI`, answers `0x5a` with dyld flags `0xff` and `0x60` with non-enforcing
state `0x86`, and preserves errors for unknown requests. A backboardd exec
logs the dyld reply. The successful, opt-in `VinceGPU` startup also publishes
the `IOAcceleratorES` discovery stub.

`0197-daisy-gpu-iosurface-import.patch` adds the deployed bridge's 40-byte
selector `0x7e` ABI: describe-only (`handle=0xffffffff`) and retained GEM
imports with stable handles on repeat import. Newly created surfaces use
the validated DMA32/WC GEM allocator after GPU service startup. Imports
reject cached, fragmented and out-of-aperture buffers; the surface state
page is separate and is never exposed through a GEM mapping. Surface and
memory references survive until the GPU is done, including client teardown.
Per-surface fences cover GPU use across clients. IOSurface locks and the
framebuffer wait before CPU access; WC stores are ordered before seed
publication. The existing 24-bpp panel conversion remains the presentation
path, so IOSurface import does not imply zero-copy scanout.

Both patches apply cleanly in sequence to the successful 0195 CI sources.
A host compiler check confirms all eleven msm request layouts against
`hakios-metal` and every surface-field offset against the deployed Mesa
bridge. The VINCE CI marker check now requires AMFI, accelerator discovery,
and IOSurface import code in the linked kernel.

The `MACHINE_CONFIG=VINCE` build passed in
[run 41150040430](https://github.com/proxer05/ios-vince/actions/runs/41150040430)
at `bb13d17`, including those linked-kernel checks:

- Kernel: `artifacts/kernel-0197/kernel.development.vince`, SHA-256
  `647b0cf5268dd4d4d938aa70ec2a3aea305022ac077dd7517c979d0292aa8d9a`.
- Temporary-boot image: `artifacts/daisy-gpu-userland-0197/daisy-boot.img`,
  17,948,672 bytes, SHA-256
  `431e933a1a9c757085e7ad7f218f68ccc61c4db4f2b945ceedd256cf0aaf183d`.
- Packaging verification passed: boot header/digest, appended Daisy DTBs,
  empty `second`, payload geometry, and embedded kernel markers. Shim,
  diagnostic arguments (`cpus=1 daisylog=1 adreno=11 daisygpu_test=1`), and
  ADT properties match 0195 except the fresh random seed. Rebuilding this
  configuration requires `VINCE_NO_FBCON=1`.

The image was temporarily booted and stayed black; see the diagnosis below.
Hardware rendering validation remains pending.

```sh
fastboot -s 6790a29e0305 boot artifacts/daisy-gpu-userland-0197/daisy-boot.img
```

The recovered log shows the backboardd dyld-policy reply, injector/EAGL
startup, and GPU user-client opens, but no Mesa submissions. A successful
0195-style memory self-test alone does not validate rendering.

## 0197 black-screen diagnosis — 2026-10-31

The user reported no logo or display after more than five minutes. The phone
was in fastboot when inspected. Temporarily booted the existing TWRP image
and recovered the persistent console and unified logs:

- `artifacts/console-gpu-userland-0197-20261031.log`: 553,322 bytes, SHA-256
  `09dd4d6002c5b5abd44f79da70ed6164389e85946b96e0a44a98c768d9fb4341`.
- `artifacts/gpu-userland-0197-logs-20261031/parsed/unified.jsonl`:
  812,584 messages, zero unresolved strings. Filter by boot UUID
  `4FAA21F2898749C08DBC62A2CC884C15` for the 31,516 new-boot messages;
  the other 781,068 belong to the earlier 0195 boot. New-boot recorded
  timestamps span `2026-10-31T00:29:04.252886272Z` through
  `2026-10-31T00:29:59.728189440Z`.

The console establishes this sequence:

1. The GPU memory self-test passes at `0xa3ff4000` (line 173), followed by
   accelerator and GPU service registration. AMFI answers backboardd with
   `flags=0xff error=0` (line 684); the old policy-not-found error is absent
   from the new boot's unified messages.
2. The deployed injector loads the GL bridge, which replaces all seven
   EAGL methods. Mesa opens the GPU user client and reports
   `OpenGL ES 3.1 Mesa 25.0.7; FD506` (line 1101).
3. Backboardd creates a 1125x2436, two-plane `b3a8` surface (line 1142).
   `userland/gpu/eagl.c:my_attachImage` accepts only non-planar BGRA/format-0
   targets with four bytes per element. It refuses this surface, followed
   by `glDrawElements: error 0x506` (`GL_INVALID_FRAMEBUFFER_OPERATION`).
4. IOSurface selector 6 is called (line 1189), then backboardd faults at
   `0x30` in `_platform_memmove`, called by
   `IOSurfaceClientGetBulkAttachments` (lines 1190–1195). Selector 6 is
   `IOSurfaceWrapClientImage`; Daisy has no handler and its default branch
   reports success with a zero-filled result, including the shared-state
   pointer. This explains the null-base attachment read. Merely storing
   selector-27 attachments does not repair that invalid surface result.
5. Five backboardd instances crash with the same fault (PIDs 57, 104, 112,
   136, 169). The sixth creates a Mesa context but cannot allocate the
   13,881,344-byte contiguous target. The console records 93 such allocation
   failures; unified logs confirm `CAIOSurfaceCreate: failed to allocate
   IOSurface (size: 1125 x 2436, format: b3a8)`.

There is no recorded kernel panic, GPU draw submission, or imported GPU
surface in this log. The framebuffer receives `display on`; that does not
establish that a frame reached the panel.

Remaining port work identified by these logs:

- Adapt Hakios's owned-surface `b3a8`/10-bit-to-BGRA normalization, including
  dropping the separate alpha plane and reporting the actual geometry.
- Implement valid client-memory surfaces (selectors 6/7 and
  `IOSurfaceAddress`) with retained mappings and a real shared-state page.
  Unsupported creation calls must return an error rather than fabricated
  success. Current Hakios rejects unimplemented selectors; Topaz patches
  0132/0134 provide the SweetSurface wrapping reference.
- Port attachment/value state (Hakios and Topaz 0133); keep wrapped cached
  memory on the copy path because Daisy's GPU import requires DMA32/WC.
- Add an ordinary-memory surface fallback when contiguous allocation fails,
  and investigate the repeated large-allocation failures before enabling
  zero-copy presentation. Mesa's existing import rejection/copy path can
  handle non-importable surfaces.

The device was left in TWRP after extraction. The navigable 0194 SMP image
remains the recovery baseline.

## IOSurface crash fixes — 0198

`0198-daisy-iosurface-client-memory.patch` addresses the observed 0197 failures:

- Selectors 6/7 and `IOSurfaceAddress` now pin and retain the caller's actual
  pixels, map a real shared-state page, and report the correct sub-page
  address. They keep cached mappings and remain ineligible for GPU import.
  Invalid ranges and unsupported selectors return errors. The last surface
  owner releases the prepared memory descriptor; failed mappings/replies
  unwind their references.
- Newly allocated `b3a8`, `w30r`, `l10r` and `w30R` targets become non-planar
  BGRA with recomputed geometry. The failing 1125x2436 target now needs
  11,071,488 bytes instead of 13,881,344. Existing caller pixels retain their
  original format.
- DMA allocation failure falls back to ordinary cached memory, allowing
  Mesa's copy path. The diagnostic boot argument `daisysurface_copy=1`
  bypasses surface DMA allocations entirely for the first rendering test;
  GEM allocations and the GPU memory self-test still use DMA32/WC.
- Per-surface values, bulk attachments, YCbCr matrix, accelerator binding
  and purgeability state are retained. Metadata updates require this
  client's surface reference. Unknown selectors no longer fabricate a
  successful empty result.
- Result construction retains its mapping reference across concurrent
  client teardown, and raced repeat lookups use the same cached-client
  protocol. Framebuffer bounds account for wrapped pixels' page offset.

References remain Hakios `MiniIOSurface.cpp` and Topaz SweetSurface patches
0132–0134, adapted for Daisy's identity-mapped GPU and copy fallback.

`python3 tools/tests/test_daisy_surface.py` applies the actual patch chain
and compiles the whole SweetSurface implementation against functional
IOKit stand-ins under AddressSanitizer/UndefinedBehaviorSanitizer. It passes
the recorded target geometry, client image/memory calls, non-null state
results, attachment bounds, value seeds, cached lookup, failed mapping and
short-output cleanup, retained lifetime, caller-address preservation, and
out-of-line results. The host test does not validate real VM/cache
attributes or GPU rendering.

The `MACHINE_CONFIG=VINCE` build passed in
[run 41152951075](https://github.com/proxer05/ios-vince/actions/runs/41152951075)
at `e28faf9`, including the linked-kernel marker checks:

- Kernel: `artifacts/kernel-0198/kernel.development.vince`, SHA-256
  `bff696a81c78f286beb2f6988086e8f66a12cfe37f615f3ddb6b8cdd07aa07fd`.
- Copy-path image: `artifacts/daisy-gpu-copy-0198/daisy-boot.img`,
  17,948,672 bytes, SHA-256
  `401b6953b1278cfb334d60f6deb853c74f5a83c5a6455e1a12d3e24e65c34cc7`.
- Verified kernel checksum/markers, boot header/digest, appended Daisy DTBs,
  empty `second`, and payload geometry. Shim and ADT match 0195 (apart from
  the fresh random seed). Arguments add only `daisysurface_copy=1` to its
  `cpus=1 daisylog=1 adreno=11 daisygpu_test=1` diagnostic configuration.
  The preserved 0194 SMP baseline checksum was also checked.

This image was temporarily booted; the rendering result is recorded below.
It uses the already deployed GPU userland. Test with:

```sh
fastboot -s 6790a29e0305 boot artifacts/daisy-gpu-copy-0198/daisy-boot.img
```

Look for `GPU surface copy-only=1`, non-planar `BGRA` targets, successful
`wrap sel 6` results, and the first `VinceGPU: submit fence=` messages.
Surface imports are intentionally bypassed in this first test. Recover
the persistent log to check whether backboardd's `0x30` fault is gone and
whether draw fences complete before moving to shared-surface testing.

## 0198 rendering result — 2026-10-31

The user reports that the UI renders and blur no longer has its software
renderer's white background, but interaction is very laggy. After the user
returned to fastboot, temporarily booted
`/smb/twrp-3.7.1_12-daisy-20260715-2011.img` and recovered the logs:

- Console: `artifacts/console-gpu-copy-0198-20261031.log`, 496,590 bytes,
  SHA-256 `99ae01535a47aa2d0024f4f3230fe604f8f8cdc750a23271cdffd01e7defbbbe`.
- Unified log: `artifacts/gpu-copy-0198-logs-20261031/parsed/unified.jsonl`;
  866,030 messages and zero unresolved strings. Filter boot UUID
  `25C39B433AFB4ECE97421BF86F4C9BA2` for 53,446 new-boot messages, recorded
  from `2026-10-31T01:02:15.249498368Z` to `2026-10-31T01:03:12.815897088Z`.
  The other two boots are older evidence; this recorded range is not a
  wall-clock measurement of the test's duration.

`copy-only=1` is active and the memory self-test passes. The console has
one Mesa context, submissions starting at fence 2 (34 BOs, four IBs), and
BGRA swaps reaching the framebuffer. No panic or backboardd crash marker
was found. This is the first hardware evidence of Mesa draws and visible
presentation, rather than just the memory self-test.

Two costs dominate the captured frame summaries:

- Readback generally takes 160–220 ms per full target. The first targets
  are 1125x2436 BGRA, with a 4544-byte stride. Copy-only mode uploads and
  reads back roughly 10.8 MiB per full frame, still on one CPU with logging.
- There are 111 failed 12 MiB GEM allocations and three failed 3 MiB
  allocations. `vm_page_find_contiguous` scans about 344,219 pages and
  yields hundreds of times without finding a suitable run. GL reports
  out-of-memory and invalid-framebuffer errors, and frames 5/6 take about
  29/36 seconds in the bridge's timing. Surface allocation fallback alone
  cannot fix Mesa's own GEM allocation failures.

The first twelve wrapped-image diagnostics include six successful wraps
and six `0x2` failures. This is `KERN_PROTECTION_FAILURE` while preparing
client memory for writing, not a null successful result. Read-only static
images need the read-only descriptor fallback already used by Hakios.

The device was left in TWRP after extraction. No partition was flashed.

## Reserved GEM pool and read-only images — 0199

`0199-daisy-gpu-reserved-gem-pool.patch` adds the diagnostic boot argument
`daisygpu_pool=N` (MiB, default off). At GPU service startup it reserves
32 MiB contiguous DMA32/WC chunks, up to 512 MiB total, before userland
fragments RAM. The next copy-path test requests 256 MiB.

GEM allocations use page-aligned best-fit spans inside this reserve. Freed
spans coalesce, reused spans are zeroed, and exhaustion reports used/peak
storage and the largest free span rather than entering another global VM
scan. Pool mode currently supports individual GEM allocations up to 32 MiB.
The allocator-backed GPU memory test also exercises the pool when enabled.
The original allocator remains selected when the pool is disabled or no
chunk could be reserved. IOSurface allocation policy is unchanged.

Each lease is an independent physical memory descriptor with a WC default
cache mode. Its last release returns the span to the pool; a live mapping
therefore prevents reuse after GEM_CLOSE. This deliberately avoids XNU's
`IOSubMemoryDescriptor`, whose mapping retains the parent rather than the
slice. Existing fence waits and stalled-buffer retention still apply.
The first four successful fence waits now log explicit completion markers.

Client-memory wrapping retries protection failures with `kIODirectionOut`.
These surfaces retain read-only cached mappings, reject write locks, and
remain on Mesa's copy path. The wrap diagnostic includes `ro` status.

Host ASan/UBSan checks:

```sh
python3 tools/tests/test_daisy_surface.py
python3 tools/tests/test_daisy_gpu_pool.py
```

These replay the surface calls and exercise the actual pool allocator,
including 111 repeated 12 MiB targets, fragmentation/coalescing, zero-fill,
mapping lifetime, descriptor failures and partial reserve initialization.
Real WC mapping, early reservation capacity and rendering performance
still require the VINCE build and a phone test.

The first build ([41155422508](https://github.com/proxer05/ios-vince/actions/runs/41155422508))
caught a compile-time bitmap bound using XNU's runtime `PAGE_SIZE`. The
bitmap now uses `PAGE_MIN_SIZE` for its static capacity, while allocations
still follow the actual page size. The pool test now passes with both
4 KiB and 16 KiB runtime pages under ASan/UBSan.

The VINCE rebuild passed in
[run 41156231189](https://github.com/proxer05/ios-vince/actions/runs/41156231189)
at `3d464f6`, including all linked-kernel marker checks. The verified image
is ready for temporary boot:

- Kernel: `artifacts/kernel-0199/kernel.development.vince`, SHA-256
  `793ec043a78ec391bf42903dbbb5017fdb1eff939345d861a8bd66a40f3b569b`.
- Image: `artifacts/daisy-gpu-pool-0199/daisy-boot.img`, 17,983,488 bytes,
  SHA-256 `aa71ae2f5be85a9889da0eceaef381769ed41c6f00107866554c9e781172e6bf`.
- Verified downloaded artifact digest, kernel checksum/markers, Android
  boot header/digest, appended Daisy DTBs, empty `second`, payload geometry,
  and unchanged 0194 SMP baseline. Shim and ADT match 0198 apart from the
  fresh random seed. The only added argument is `daisygpu_pool=256`;
  `cpus=1 daisylog=1 adreno=11 daisygpu_test=1 daisysurface_copy=1` remains
  selected to compare allocator behaviour against the rendering 0198 boot.

```sh
fastboot -s 6790a29e0305 boot artifacts/daisy-gpu-pool-0199/daisy-boot.img
```

The hardware checks for this image are `GEM pool v1 reserved=256 MiB`, the
memory-test PASS, `completed fence=`, and successful `wrap ... ro 1`
results. If a pool allocation fails, its used/peak/largest-span diagnostic
will distinguish capacity from fragmentation. Compare the multi-second
frame stalls and GL allocation errors with 0198. Readback still uses the
copy path; shared surfaces and the eight-core performance test follow
after this allocator check. Existing deployed userland is sufficient.

The user tested 0199 and reports clean blur with some missing shadows,
but continued severe lag. The recovered console confirms the full 256 MiB
reserve, completed draw fences, successful read-only wrapping, and no
logged allocation failures. At 600 frames the bridge still averages about
149 ms/frame copying out. See the end-of-day handoff for exact evidence.
The user requested only log collection and preservation before stopping;
shared-surface and SMP testing have not been started.

## Managed pool surfaces and A506 pitch — 0200

Work resumed at the user's request. Review of the 0199 console identified
the repeated backtraces as `IOMemoryDescriptor physical with managed page`,
emitted when DEVELOPMENT XNU maps the pool's physical descriptors. Patch
`0200-daisy-gpu-managed-pool-surfaces.patch` describes each independent
lease through the reserve's wired kernel virtual address, auto-prepares
its persistent VM entry, and checks the complete physical span against
the reserved DMA32 range. The descriptor tears down that VM entry before
returning the span. User mappings still retain the lease through GEM_CLOSE.

Owned IOSurfaces now share the early reserve and track their allocation
and kernel alias explicitly. Cached/client-memory surfaces retain their
fallback. Mesa 25.0.7 `fd_resource_from_handle()` requires 64-pixel rows on
A506 (`gmem_align_w=64`), so newly allocated non-planar BGRA targets now
report 256-byte-aligned rows: 1125 pixels use 4608 bytes rather than 4544.
Wrapped images keep their actual pitch. The framebuffer's shadow surface
uses the same alignment and its returned pitch feeds the panel converter.
The deployed EAGL bridge already supports a GPU blit into imported surfaces.

ASan/UBSan surface and pool checks pass, including pooled surface accounting,
BGRA geometry, cached/read-only wrapping, mapping lifetime, rejected DMA
spans, descriptor failures, and 4 KiB/16 KiB pages. Hardware validation is
pending. The next image enables shared surfaces by removing
`daisysurface_copy=1`; it keeps `cpus=1`, `daisylog=1`, `adreno=11`,
`daisygpu_test=1`, and `daisygpu_pool=256` for comparable diagnostics.

### CPU/GPU clock investigation

The user suggested maximum CPU/GPU frequencies. The 0199 console measures
CPU 0 at 1401 MHz (APCS PLL L=73) and records GFX3D CFG=0x507, the stock
100 MHz GPLL0_DIV2 configuration. Stock KGSL reports a 650 MHz GPU maximum.
Pinned Xiaomi source `6421aa5cdc56e9ef7c6329e0a05164503e1513ef`, saved in
`artifacts/daisy-clock-reference/`, confirms:

- 650 MHz uses GPLL3 at 1300 MHz with its divide-by-two output.
- GPU corner 7 uses the CX TURBO RPM level, bypasses the graphics LDO,
  and selects graphics memory-acceleration corner 2.
- The ordinary CPU speed-bin table reaches 2016 MHz; the actual fuse bin
  and CPR4/S5 voltage setup must be used rather than the table alone.

Clock changes need that power sequencing and acknowledgement; the current
touch RPM helper only supports its two fixed LDO votes. No maximum-frequency
option is implemented in 0200. The existing `vincemhz` accepts 800/1036 and
otherwise selects 1401.6 MHz, so `vincemhz=2016` would not select 2016 MHz.

### Remaining shader failure located

Filtering the 0199 unified log to boot
`08EEBA6266604C208A0E06157012A8B8` and sorting by timestamp puts the two
`glDrawElements: error 0x502` messages immediately after failed bridge
programs: the vibrant colour matrix, then group-over/group-flat.
`artifacts/daisy-0200-gl-error-context.log` preserves the focused context.
All three shaders require `GL_EXT_shader_framebuffer_fetch`.
Mesa 25.0.7 `freedreno_screen.c:573` only enables `caps->fbfetch` for A6xx,
but `eagl.c` attempts these private shaders whenever `fetch_offered` is
true (the default), without checking hardware support. The resulting
unlinked programs also receive uniform-location queries. The A6xx cache
flush workaround cannot fix unsupported A5xx shader compilation.

A follow-up userland fix needs a sampled destination-copy path for those
private effects, proper capability detection, and compile/link diagnostics.
Simply advertising the extension or applying the A6xx workaround is not
sufficient. The public extension string already comes from Mesa, so
CoreAnimation's own shaders can use its fallback. 0200 tests presentation
with the verified deployed userland first.

### 0200 build and temporary boot

VINCE DEVELOPMENT CI
[41205637548](https://github.com/proxer05/ios-vince/actions/runs/41205637548)
passed at `0e8bad6`, including both new linked-kernel markers. The downloaded
artifact digest, kernel checksum, boot header/digest, payload layout,
appended Daisy DTBs, empty `second`, and preserved 0194 baseline all passed
verification. Packaging scripts and output are saved under `artifacts/`.

- Kernel: `artifacts/kernel-0200/kernel.development.vince`, SHA-256
  `a0b0c99d1658cceae3a72da0a3e8c89ae694d2619b85ea98c3be9c472b9d7376`.
- Image: `artifacts/daisy-gpu-shared-0200/daisy-boot.img`, 17,983,488 bytes,
  SHA-256 `6c1ef32f2053d04b59a3cbffcc8bcb493008cd9042f5615ae86eec013767fee2`.
- `fastboot -s 6790a29e0305 boot` successfully transferred and started this
  image. No partition was flashed. Awaiting the user's visual result;
  successful fastboot transfer does not establish successful XNU rendering.

After a UI exercise, recover the console using the established temporary
TWRP/static SleuthKit path. Look for `pool v2`, memory-test PASS, absence of
managed-page backtraces, `VinceGPU: import surface=`, surface row bytes 4608,
and EAGL GPU-blit/shared counts with reduced copy-out time. Check pool
exhaustion because GEM and surfaces now share its 256 MiB. Keep clock and
eight-core tests for after this comparison. Current editable XNU is
`artifacts/daisy-0200-work/xnu/`; 0200 changes are exported in the committed
patch, on top of its local `3d19c7c` verified 0199 baseline.

### 0200 hardware result and 0201 correction

The user reported that 0200 was not smoother and that the app-switcher blur
no longer rendered. The recovered console
(`artifacts/console-gpu-shared-0200-20261031.log`, 474,293 bytes, SHA-256
`a4a9d2191a1fee55def25f7b6a9f1bada79fe2edbf08b94a1abdc571033ce55b`)
shows the managed-pool mapping fix worked: no `IOMemoryDescriptor physical
with managed page` backtraces, the pool v2 marker, memory-test PASS, and
successful imports (`import surface=2 ... bpr=4608`).

The shared reserve then exhausted: `GEM pool exhausted request=11227136
used=216512 KiB ... largest=10840 KiB`, followed by repeated
`DMA allocation failed; cached copy fallback`. Surfaces and GEM were
competing for the same 256 MiB, and 11.2 MiB surface spans fragmented the
reserve. The frame lines changed from `1 drawn in place` to `1 picture
copied out`, and later frames still averaged about 170-200 ms copying out.
The app-switcher blur was absent even though the vblur shaders compiled and
the levels were made. The unified log for the 0200 boot
(`D41B0A7A28494B18A911ED50975DAD92`) shows why: after the surface failures,
texture creation begins failing with `glTexImage2D: error 0x505`
(`GL_OUT_OF_MEMORY`), followed by `glDrawElements: error 0x506`
(`GL_INVALID_FRAMEBUFFER_OPERATION`) and `glFlush: error 0x506`. The shared
reserve left no room for the blur's intermediate textures, so the effect had
nothing to read. The 0199 log never exercised the variable-radius path, so
the blur regression is pool pressure, not a new shader fault.

`0201-daisy-gpu-surface-dma-opt-in.patch` makes shared surface memory
opt-in (`daisysurface_dma=1`). The default is the validated cached-copy
path, leaving the reserve for GEM only. The next image also adds
`glfetch=0`, so the bridge does not attempt its A6xx-only
framebuffer-fetch shaders and CoreAnimation uses its own fallback. This
should restore the 0199 blur and remove the two `0x502` errors. The
reserve mapping fix and A506 pitch normalization remain in place.

### 0201 hardware result

The 0201 image (`artifacts/daisy-gpu-copy-0201/daisy-boot.img`, kernel
`3558f5ff...`, image `6db168ba...`) booted with `fastboot boot`. The user
confirmed the app-switcher blur returned and the missing shadows looked
better. The console (`artifacts/console-gpu-copy-0201-20261031.log`,
2,304,951 bytes, SHA-256
`b010acb99c6663220f95eacc7cb09a48bce24ad6ba1f139f5bd693db317f9c62`)
shows `VinceGPU: surface DMA off; cached copy path otherwise`, no
`0x502` errors, and `0 errors` in the frame censuses. Smoothness was
unchanged, as expected: every frame still reported about 170 ms
`copying out` (frame 120: 173,978 us; frame 300: 171,382 us).

A second limit appeared after frame 500: with surfaces off, GEM textures
filled the 256 MiB reserve. `GEM pool exhausted request=32768 used=262128
KiB ... largest=16 KiB` was followed by 4 KiB failures with `largest=0`,
then `backboardd` died with `daisy-crash: signal 11 at address 0x1c`.
Mesa does not handle a NULL GEM allocation. The pool filled because the
app switcher keeps many full-screen textures alive; this is not a new
surface leak, but the fixed reserve is too small for that peak.

`0202-daisy-gpu-gem-fallback.patch` lets a buffer of at most 1 MiB come
from the ordinary contiguous allocator when the reserve is full, instead
of returning NULL and crashing. Larger requests still fail rather than
trigger a multi-second global VM scan. The 0202 test uses a 384 MiB
reserve and keeps `glfetch=0`; a second image enables
`daisysurface_dma=1` so the shared path can be compared with the same
fallback in place.

### 0202 hardware result and 512 MiB test

VINCE build [41218400794](https://github.com/proxer05/ios-vince/actions/runs/41218400794)
on `ci-macos15` passed with Xcode 16.0, and the shared image reached the UI.
The user reports a small improvement. Kernel SHA-256:
`d75fbd2916b796c2f363ba9738fd188f7db20ed117a9871542abb2e4d53f55cf`.
Both copy/shared images are under `artifacts/daisy-gpu-{copy,shared}-0202/`.

The recovered console `artifacts/console-gpu-shared-0202-20261031.log`
(254,346 bytes, SHA-256
`69510359d9e5724de85eba2b8ced28abdb7ec20775108f60b47d2bc365f61a03`)
confirms the full 384 MiB reserve, memory-test PASS, shared imports, and early
draw-in-place frames around 30–60 ms (frame 100: 26.8 ms). After frame 120,
11,227,136-byte surface allocations fail at `used=304552 KiB`,
`largest=10840 KiB`; later frames use cached copy-out again at 170–200 ms.
There is no crash marker in this capture, but also no successful small-buffer
fallback marker: this run does not establish that the fallback prevented a crash.

The same kernel was repackaged with `daisygpu_pool=512` and temporarily booted:
`artifacts/daisy-gpu-shared-0202-512/daisy-boot.img`, SHA-256
`cd149fcea729173f54e55b9a4e1a9cd5e352e756873586a4925eafadd57d0e97`.
The user supplied three later failures at `used=417868`, `418484`, and
`419100 KiB`, all with `largest=10840 KiB`. Increasing capacity delayed the
failure without removing fragmentation. A 1 GiB reserve is inappropriate for
the current `0x57f00000` (1407 MiB) XNU RAM window; investigate packing and
small-buffer placement while keeping the 512 MiB test budget.

The user's complete 512 MiB console is already saved as
`artifacts/console-gpu-shared-0202.log` (492,526 bytes, SHA-256
`cc2e66a77b6a874ef3d0a00ef21afe33135521e5911d6ef5ac01de0be4593dc8`).
Its first failure is earlier, after frame 200 at `used=400536 KiB`.
Frame 200 still draws in place in 26.2 ms; frame 220 copies out in 229.2 ms.
It reaches frame 1000 with zero logged GL errors or signal crashes, but
contains 374 failed global contiguous-page scans. The surface allocator's
large ordinary-allocation retry bypasses the GEM allocator's bounded failure.

`32 MiB - 2 * 11227136 bytes = 10840 KiB` exactly. Chunk-boundary waste is
therefore a specific explanation for the repeated largest-span value, even
before accounting for smaller objects. At a fixed 512 MiB budget, 16 x 32 MiB
chunks can hold 32 such surfaces; 14 x 36 MiB plus an 8 MiB remainder could
hold 42. This is a packing calculation, not a replay of the mixed workload.
The next allocator experiment should consider that geometry and suppress the
large post-reserve VM scans. The 0203 clock build keeps the current allocator.

### 0203 opt-in 650 MHz clock

`0203-daisy-gpu-650mhz.patch` adds `daisygpu_mhz=650`, requiring full
`adreno=11` startup, no skipped steps, and `daisygpu_test=1`. It runs after
the existing low-clock setup, before CP startup and the allocator-backed
GPU self-test. CI now uses the macos-15/Xcode 16.0 combination that built
the boot-tested 0202 kernel.

The sequence follows the pinned MSM8953 source listed in the clock section:

1. Require the observed LK BHS handoff (`PWRSWITCH_CTRL=0x80000038`,
   `STATUS=0x3`), live CX/GX domains and the acknowledged 100 MHz root.
2. Send active-set `smpa` resource 2, `swen=1`, `vlvl=384` (CX TURBO).
   Wait for the matching non-error RPM ACK. Reuse the SMD channel for the
   later FT5446 votes; GPU startup completes before that thread is created.
3. Clear graphics memory-acceleration bit 0 at `0x0194415c` (corner 2)
   after the voltage vote, and recheck BHS and memory-timing readback.
4. For an idle GPLL3 at GCC offset `0x22000`, use the stock configuration
   and /2 output, calibrate at 1500 MHz with output disconnected, then slew
   to 1300 MHz. Require update completion, lock and rate-register readback.
   A matching already-running PLL can be reused; other active/FSM states
   are rejected rather than reprogrammed.
5. Switch GFX3D at offset `0x59000` to source 2/div1 (`CFG=0x201`),
   checking update and root/branch status. A failed switch restores the
   startup root; failed restoration stops bringup before CP/service startup.

The CX vote remains active until reboot. The path requires the existing
BHS supply state and does not attempt an unobserved live LDO transition.
The successful marker reports configured frequency, not an independent
frequency-counter measurement. Hardware validation is pending.

The earlier investigation of `clock-gpu-cobalt.c` was for the wrong platform:
Daisy uses `clock-gcc-8953.c`, GPLL3 and legacy SMD RPM, not Cobalt GPUCC/Fabia.
Hakios `drivers/gpu/hakios.adreno.c` at
`85bb05f0a4ed09e4e275f64047afed0122f59180` confirms its frequency selection is
through the A642L's GMU/RPMh path, so its register sequence is not transferable.
The existing [hakios-metal](https://github.com/oosexgang/hakios-metal) userland
reference remains relevant to rendering, not MSM8953 power programming.

Host verification passed:

```sh
python3 tools/tests/test_daisy_touch_rpm.py
python3 tools/tests/test_daisy_gpu_clock.py
```

These apply the real patch chain and check exact touch/CX packets, shared
channel lifetime, ACK errors/timeouts, power-before-clock ordering, PLL
calibration/slew and lock failures, opt-in/handoff rejection and RCG rollback.
The MMIO peer does not establish hardware clock or voltage behaviour.

0203 CI is [41226126241](https://github.com/proxer05/ios-vince/actions/runs/41226126241)
at `d55bcec`. Packaging helper: `artifacts/build-daisy-0203.py`; use
`--dma --pool 512` for the clock test, or `--mhz 100` for a control image.
Pinned source checksums are in
`artifacts/daisy-clock-reference/GPU-650-SHA256SUMS`.

### 0203 build and temporary boot

CI 41226126241 passed, including linked-kernel markers. The downloaded
artifact checksum and packaged boot header/digest, appended DTBs, empty
`second`, shim/ADT geometry and preserved 0194 baseline all verified.

- Kernel: `artifacts/kernel-0203/kernel.development.vince`, SHA-256
  `e86b4fa21a7b41961fc78e5042a10c48b2a5a1c562bd0c69232c93969b1d31a1`.
- Image: `artifacts/daisy-gpu-650-0203/daisy-boot.img`, 17,979,392 bytes,
  SHA-256 `4a2b8c1ffc673672586ea62ca1cac6fe6d1cba8bdf446e8ed9b41df9ed646bf1`.
- Arguments retain `cpus=1`, `adreno=11`, `daisygpu_test=1`,
  `daisygpu_pool=512`, `glfetch=0` and `daisysurface_dma=1`, adding only
  `daisygpu_mhz=650` to the previous 512 MiB test configuration.

`fastboot -s 6790a29e0305 boot` transferred and started the image successfully.
This confirms the temporary boot command, not successful iOS startup or a
650 MHz hardware clock. Await the UI result and recover the console to check
`GPU CX TURBO level384 result 0`, `configured 650 MHz`, memory-test PASS,
touch power/report success, frame timings and pool failures.

### 0203 clock hardware result and direct-target experiment

The user reports a successful boot with similar speed. Recovered console:
`artifacts/console-gpu-650-0203.log`, 207,368 bytes, SHA-256
`8c6ed0c7b999c8e864011b757e9197e0b3dd75429c9f034018daf79b004fb4ca`.
It confirms:

- `GPU CX TURBO level384 result 0` after the matching `smpa2/vlvl` ACK.
- `configured 650 MHz`: GPLL3 `0x80000007`, L=67, alpha
  `0xb5/0x55555600`, GFX3D CFG=`0x201`, BHS ACK and memory-acceleration
  corner 2. This is the hardware register/lock result, not an independent
  frequency-counter measurement.
- Full 512 MiB reserve and allocator-backed GPU memory-test PASS.
- The subsequent FT5446 L10/L6 votes receive ACKs with message IDs 2/3;
  touch reports in this short capture are all idle/malformed, with no
  valid contacts. Contact input still needs checking in the next test.
- Only 19 frame summaries, no logged pool failure, GL error or signal crash.
  Several full-size attachments take 0.49–0.64 seconds in the bridge's
  `before it for the IOSurfaces` timing. This is not a sustained app-switcher
  stress test and cannot establish longer-term pool behaviour.

The EAGL bridge defaults `blit_targets=1`. `my_attachImage()` imports a
mirror surface, keeps a separate ordinary render texture, then calls
`surface_changed()` and `surface_to_texture()` to populate that texture.
Thus every new target still reads/hashes and uploads roughly 11 MiB from
the WC surface, and retains two full-size buffers, even though presentation
reports `drawn in place`. This explains a major cost outside GPU execution.

The existing `glblit=0` option instead calls `surface_shared()` directly
for eligible non-inverted BGRA targets. It avoids both the initial copy
and the second target allocation. The root-build binary contains this
option and `glfetch=0`; no userland rebuild is needed. The imported bridge's
mirror default addressed A6xx framebuffer-fetch artifacts, so test blur,
icons and Control Center with Daisy's existing `glfetch=0` fallback.

Packaged and temporarily booted `artifacts/daisy-gpu-direct-0203/daisy-boot.img`
(17,979,392 bytes), SHA-256
`7e3f4680272b575b989bd22e3f4f84273e26538127c3b0312b75d97080a80528`.
It uses the same verified 0203 kernel, 650 MHz request, 512 MiB reserve and
shared DMA, adding only `glblit=0`. All packaging checks passed. Await the
visual/input result, then recover logs for `-attachImage ... : its memory`,
lower attachment/upload times and whether reserve exhaustion is delayed.

### 0203 direct-target result: smoother, then allocation failure

The user reports smoother swipes (still subjectively below 20 fps), followed
by a black screen after several swipes. Recovered console:
`artifacts/console-gpu-direct-0203.log`, 274,260 bytes, SHA-256
`2656c8fcf8f043d30f73ca740f28ba1ac5ba75115a50f07a1c7748ce56183271`.
650 MHz configuration, GPU memory-test PASS and valid FT5446 contacts are
present. Direct targets reduce attachment time: frame 30 is 16.8 ms with
248 us before it for IOSurfaces; frame 200 is 18.2 ms. These are bridge
timings, not measured panel frame rate.

After frame 260, a surface allocation fails at `used=397600 KiB` and
`largest=10840 KiB`. The surface path retries the global contiguous
allocator, then falls back to cached storage. Mesa's replacement 12 MiB
texture also cannot fit: `glTexImage2D: error 0x505` is followed by
`glDrawElements`/`glFlush: error 0x506` (incomplete framebuffer).
The capture contains two out-of-memory errors, 58 invalid-framebuffer
errors and 38 failed global contiguous scans, with no logged signal crash
or VinceGPU stall/timeout. This establishes allocation failure as the
immediate rendering failure; it does not demonstrate a shader regression
or establish why so many buffers remain live.

### 0204 pool packing and bounded surface fallback

`0204-daisy-gpu-pool-packing.patch` changes the reserved allocator to 36 MiB
chunks. A 512 MiB request allocates 14 full chunks plus an 8 MiB final chunk;
the cap remains 512 MiB. Each chunk records its actual page count, and all
searches stay within that count. Requested budgets are exact rather than
rounded up to a whole chunk. Best-fit allocation can use the small final
chunk and freed tails for smaller objects.

Three 11,227,136-byte surfaces or three 12 MiB textures fit in each full
chunk. Host tests of the actual patched allocator fit 42 of either size
within the reserve, versus 32 with the old layout. This is a controlled
capacity test, not a replay of the phone's mixed allocation lifetimes.

`vince_surface_allocate()` now uses only `gpuGemAllocate()`'s fallback
decision: no second ordinary allocation after a failure. With a reserve,
only requests up to 1 MiB may fall back to ordinary contiguous storage.
That limit also applies if all early reserve allocations fail. With the
reserve disabled, the ordinary allocation path remains available.

On pool misses, new `pool leases takes=... returns=... live=...` counters
show reservations and returns (including descriptor-construction rollback).
They help check reclamation without treating any still-referenced range
as free. Persistent lease descriptors, mapping lifetimes and GPU fences
continue to govern reuse.

`python3 tools/tests/test_daisy_gpu_pool.py` passes with ASan/UBSan at both
4 KiB and 16 KiB runtime page sizes. Coverage includes 42 full-size mixed
GEM/surface allocations, exact/capped budgets, the final partial chunk,
partial/failed reserves, no large VM retries, one small fallback attempt,
111 zero-filled reuses, disjoint physical spans, failure unwind and ranges
held by user mappings. Hardware validation is pending; the next image
retains 650 MHz, `daisygpu_pool=512`, `daisysurface_dma=1`, `glfetch=0` and
the smoother `glblit=0` direct path.

### 0204 build and temporary boot

VINCE DEVELOPMENT [CI 41228965144](https://github.com/proxer05/ios-vince/actions/runs/41228965144)
passed at `84cc108`, including the pool v3 and lease-counter linked-kernel
checks. Verified artifacts:

- Kernel: `artifacts/kernel-0204/kernel.development.vince`, SHA-256
  `fdeb2c14eb12f54ac740915b72bd7b999b5d050758c27c835c930700ca5e114f`.
- Image: `artifacts/daisy-gpu-direct-0204/daisy-boot.img`, 17,979,392 bytes,
  SHA-256 `c3db45bb397ade6487de1ceadb355e9ae5f3d0caf597cdf2357b39159e036478`.
- Packaging: `artifacts/build-daisy-0204.py --dma --direct --pool 512`,
  with output recorded in `artifacts/daisy-0204-direct-package.log`.

Artifact checksum, boot header/digest, appended Daisy DTBs, empty `second`,
shim/ADT geometry and preserved 0194 SMP baseline all passed verification.
The arguments match the previous direct-target test, including `cpus=1`,
`adreno=11`, GPU memory test, 512 MiB, shared DMA, 650 MHz, `glfetch=0` and
`glblit=0`.

The phone was in TWRP. `adb reboot bootloader` followed by `fastboot boot`
successfully transferred and started the image. Await the user's swipe
test and recovered console before claiming iOS startup or stability.
Check full `reserved=512 MiB chunks=15`, pool v3, memory-test PASS, touch,
and any pool misses/lease counters preceding rendering errors.
### 0204 direct-target hardware result

The user reports the clock area is still half broken with missing shadows,
the blur is still broken, and rendering is still slow. The recovered console
(`artifacts/console-gpu-direct-0204.log`, 423,093 bytes, SHA-256
`6038671d0e9fe9986d4377cb5125c313d35293f1e74018e1cb4bf18885fd23d3`)
confirms the full startup: CX TURBO ACK, `configured 650 MHz`, pool v2/v3,
the full 512 MiB reserve, memory-test PASS, eight `import surface=` results,
and completed submit fences. No signal crash appears in the capture.

The direct-target path removed the attachment cost as intended (frame 220:
0 us before it for the IOSurfaces), but the copy-out remains the dominant
cost: frame 220 155,413 us, frame 240 163,617 us and frame 260 151,007 us
copying out of 46 frame summaries.

The pool then exhausted with the lease counters exposing the defect:

```text
VinceGPU: GEM pool exhausted request=11227136 used=502320 KiB peak=502320 KiB largest=3972 KiB
VinceGPU: pool leases takes=539 returns=0 live=539 reserved=524288 KiB
```

Eight exhaustion messages follow, with 20 `glTexImage2D: error 0x505`
(GL_OUT_OF_MEMORY) and 40 `glDrawElements`/`glFlush: error 0x506`
(incomplete framebuffer) afterwards; only two <=1 MiB buffers escaped
through the 0202 ordinary-allocator fallback. `returns=0` across 539 takes
means no lease was ever reclaimed.

A host reproduction (`artifacts/daisy-0204-pool-lifetime-repro.log`,
4,955 bytes, SHA-256
`6eab233a1b065d3b0c59b48f7d3a8bb4f310a8ec70911b0a4f999bbb65df6e3d`) catches
the cause as an AddressSanitizer heap-use-after-free:
`VinceGPUPoolMemory::free()` reads its lease members after
`IOGeneralMemoryDescriptor::free()`, whose `OSObject::free()` deletes the
object. On hardware the same read can observe freed memory, so the
`pages != 0` guard silently skips the reclaim and the span is never returned
to the reserve.

A mirror-defaults image for the next boot was also packaged
(`artifacts/daisy-gpu-mirror-0204/daisy-boot.img`, 17,979,392 bytes,
SHA-256 `c03ce4521df3a80202b496e76e4bf21571c302beedbc1cf18c7e5b9b99e54050`,
the same verified 0204 kernel without `glblit=0`); it has not been booted.

### 0205 pool reclaim

`0205-daisy-gpu-pool-reclaim.patch` fixes the lease lifetime:
`VinceGPUPoolMemory::free()` saves `chunk`/`first`/`pages` before the base
call, marks the span free only after the prepared VM entry has been torn
down, and reports the first four reclaims as
`VinceGPU: pool reclaimed pages=`. The startup marker becomes
`VinceGPU: pool v4: reclaim leases after descriptor teardown`. Live mappings
still retain the lease through GEM_CLOSE; a span whose free never runs stays
reserved, as before.

The host harness now models the real `OSObject` contract — `release()`
destroys the object and `free()` deletes `this` — so this class of bug fails
the ASan test instead of hardware. A new `checkLeaseDuringTeardown` asserts
the complete span stays reserved while the descriptor teardown runs, so a
concurrent allocator cannot reuse it sooner.
`python3 tools/tests/test_daisy_gpu_pool.py` passes both 4 KiB and 16 KiB
runtime pages, including 111 zero-filled reuses, disjoint leases and mapping
lifetime; `test_daisy_surface.py`, `test_daisy_touch_rpm.py` and
`test_daisy_gpu_clock.py` pass unchanged. The workflow linked-kernel check
now requires the v4 marker and a reclaim log. Hardware validation needs the
next VINCE CI build and a phone boot: `returns` should advance with client
teardown, but the exhaustion point only moves past the 539-take ceiling if
userland actually releases its buffers.

### 0205 build and temporary boot

CI [41238531547](https://github.com/proxer05/ios-vince/actions/runs/41238531547)
passed at `52422d0` in 9m46s, including the pool v4 and reclaim
linked-kernel checks. Verified artifacts:

- Kernel: `artifacts/kernel-0205/kernel.development.vince`, SHA-256
  `c3e2ca16d68f7dcb49f89509c301c7fcf2a46e7753e365c63f4f4c15cb5c63bf`.
- Image: `artifacts/daisy-gpu-reclaim-0205/daisy-boot.img`, 17,979,392
  bytes, SHA-256
  `825344be25fcbf5773e236ab416518c104ce8e7b490f452a6e6a75ce40a95b2b`.
- Packaging helper: `artifacts/build-daisy-0205.py --dma --direct --pool
  512`; kernel checksum/markers, boot header/digest, appended Daisy DTBs,
  empty `second`, payload geometry, shim/ADT geometry and the preserved
  0194 SMP baseline all passed verification.

The arguments match the 0204 direct-target test — `cpus=1`, `adreno=11`,
GPU memory test, 512 MiB, shared DMA, 650 MHz, `glfetch=0`, `glblit=0` —
so the reclaim behaviour is comparable boot for boot:

```sh
fastboot -s 6790a29e0305 boot artifacts/daisy-gpu-reclaim-0205/daisy-boot.img
```

Look for `VinceGPU: pool v4: reclaim leases after descriptor teardown`,
`pool reclaimed pages=` entries whose `returns` counter advances with
client teardown, the memory-test PASS, and whether the
`GEM pool exhausted` point moves past the previous 539-take ceiling. A
boot whose workload never releases buffers will still exhaust; that is a
userland lifetime question, not a reclaim failure.

### 0205 hardware result: working reclaims, real direct rendering, one blank first boot

Two temporary boots of the verified 0205 image on 2026-10-31.

The first boot showed a black screen after the single Apple-logo splash.
Recovered console: `artifacts/console-gpu-reclaim-0205.log`, 137,268 bytes,
SHA-256 `ae1815da2ee1a0ff2e0067d6ae1994b9b00cdc25d7f44aecdb36314c335d4f03`.
It records the full 650 MHz startup, pool v4, memory-test PASS and the
reclaims working immediately (`returns=1..4`, including reclaims between
fence-4 submission and completion). Three frames rendered directly
(`0 pictures copied out, 1 drawn in place`, 27-32 us copying out) with one
`SWEET FB: swap`, then `SWEET FB: display off` and no further display-on
event; GPU submissions stop with the compositing (the fence-completion log
is capped at four markers, so no stall can be inferred from fence 5). The
user's volume-key presses reached HID clients, but no power-key press
follows the blank: the boot is consistent with the auto-blank never being
woken, not with a GPU or reclaim fault. The unified log (boot
`DA58B4B0053043F7A0AFA45B508CBF07`) ends at 57 s while the console
continues to ~310 s of normal daemon startup; no panic or crash appears.

The second boot is the first fully working direct-rendering boot. User
report: the screen works, everything renders correctly, and it feels
smoother. Recovered console:
`artifacts/console-gpu-reclaim-0205-run2.log`, 228,126 bytes, SHA-256
`b4a6f6bef5696f049de96759c28dfed67b35ce11c5dc6db8cb05d9b6a6cbc5cb`:

- 55 frame summaries and 16 framebuffer swaps, zero `0x502`/`0x505`/`0x506`
  GL errors, zero pool exhaustion, zero pool-full fallbacks, no signal
  crashes, and valid FT5446 contacts (834-1164 valid of ~11.7k reads).
- Reclaims cover full-size surfaces: `pool reclaimed pages=2741` (a
  10.7 MiB span) at `returns=3`, plus 4- and 8-page spans; the exhausted
  0204 state never returned a single lease.
- Frame cost collapsed versus 0204: frame 240 totals 22,430 us with
  53 us copying out, frame 260 9,276 us, frame 280 10,445 us, all
  `1 drawn in place`. In 0204 the same configuration logged
  `1 picture copied out` at 151-164 ms per frame because DMA allocation
  failures had pushed surfaces onto the cached copy path; with the reserve
  reclaiming, surfaces get DMA spans and the direct path actually engages.

Frame summaries are bridge accounting, not a measured panel rate, and this
capture is not a sustained app-switcher stress test. The next leads are a
longer stability soak and the user's suggestion to pull fresh changes from
`oosexgang/hakios-metal` and the rest of that stack.
