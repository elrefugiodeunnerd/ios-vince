# Daisy CoreAnimation Metal test

The flat A506 draw passed all 4096 pixels with `hakios-metal` `f0d25bc`.
This test moves backboardd onto the upstream CoreAnimation Metal plug-in.
Full UI rendering is still experimental: the A5xx encoder currently serves
one linear RGBA8/BGRA8 target and nonindexed triangles, with no blending,
vertex attributes, depth, or MSAA.

## Build and installation

- XNU patch `0210-daisy-metal-discovery.patch` publishes `MetalPluginName`
  and `MetalPluginClassName` on `IOAcceleratorES` when `metal=1` is given.
  The kernel remains `MACHINE_CONFIG=VINCE`.
- The unified `.github/workflows/userland.yml` builds `HakiosMetal.bundle`
  from `oosexgang/hakios-xnu-kernel` `96ffbc532e970f3540885755d457e401e04d4707`,
  with `userland/gpu/metalca-a5xx.patch` and `a5xx-port` core `c2c6b31`.
  All 276 host tests pass, including the A5xx unported-2D refusal regression.
- Both render and compute pipelines use device-aware compilation, including
  function constants. Fragment specialisation preserves the original chip.
- A5xx clear-only passes, texture blits, and unaligned buffer copies refuse
  before reaching the A6xx-only 2D encoder. Their refusal messages identify
  missing work encountered by the UI.
- Stage the complete GPU artifact (including `HakiosMetal.bundle` and
  `metalca/`) in `/hdd/ios-daisy-root/binaries`, preserving previous staging.
  Run `/hdd/ios-daisy-root/build_root.sh` to rebuild userdata.
- The root builder installs the bundle under `/System/Library/Extensions`
  and assembles QuartzCore's `default.metallib`: this iOS image's four uber
  shaders from `default.pipelinelib/functions`, plus the CI Mac's AIR64
  library. Shader binaries stay in artifacts/root images. The plug-in picks
  each pipeline's actual iOS shader variant at run time.

## Boot

```sh
python3 tools/build_daisy_metalca.py \
  artifacts/kernel-0210/kernel.development.vince \
  artifacts/daisy-gpu-metalca-0210
```

The packager verifies the downloaded kernel checksum and derives arguments
from the pixel-validated 0208 image: 512 MiB reserve, shared WC surfaces,
verified 650 MHz sequence, full `adreno=11` and `daisygpu_test=1`, and the
proven touch configuration. It selects `metal=1 gl=0`. The injector defers
to Metal before running smoke tests or loading EAGL. Upstream's default
process gate exposes this device to backboardd; `metal_all=1` exposes it to
other processes as a separate experiment.

After installing rebuilt userdata, use only:

```sh
fastboot -s 6790a29e0305 boot artifacts/daisy-gpu-metalca-0210/daisy-boot.img
```

The packager checks appended Daisy DTBs, empty boot-image `second`, shim/ADT
consistency, and the preserved `daisy-touch-hid-0194-smp` baseline checksum.

## Evidence to collect

Return the phone to TWRP, then:

```sh
python3 tools/pull_daisy_console.py artifacts/console-metalca-0210.log --serial 6790a29e0305
```

Check kernel plug-in discovery, `HakiosMetal[backboardd ...]: device:` with
chip `0x5000600`, pipeline compilation/refusals, submissions/completions,
backboardd/SpringBoard exits, and visible screen updates and touch response.
A plug-in load or completed fence alone does not establish a working Metal
UI. The upstream plug-in can skip unsupported work while logging it; those
messages must be counted alongside any visible rendering.
