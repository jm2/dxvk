# kisakcod macOS profile (jm2/dxvk fork)

The `kisakcod-macos` branch carries what kisakcod's macOS client needs to run
DXVK Native's D3D9 on Apple GPUs through **KosmicKrisp**, Mesa's conformant
Vulkan driver on Metal (Homebrew `mesa`, macOS 26 or later). kisakcod pins the
branch by commit and builds it with:

```
meson setup build --buildtype=release -Dnative_sdl3=enabled \
  -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false \
  -Dnative_macos=true
```

At run time, select the driver with
`VK_DRIVER_FILES=$(brew --prefix mesa)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json`.

## Patches

| Patch | Status | Why |
|---|---|---|
| Build on macOS: `__APPLE__` compat shims, `<cstddef>`, no GNU `--version-script` on darwin, `tzcnt` overload by width | upstream PR doitsujin/dxvk#5962 | DXVK Native did not compile on macOS. |
| `getExePath` via `_NSGetExecutablePath`, `libvulkan.1.dylib` loader names | fork | `getExePath` fell off the end of the function and trapped, and the loader was not found. Upstream will see this again only once DXVK runs on a macOS driver without fork-only relaxations. |
| Depth clip without `VK_EXT_depth_clip_enable`: `depthClampEnable = !depthClip`, and the depth-clip struct is chained only when supported | fork (upstreamable) | DXVK always enables depth clamp and relies on the extension to turn clipping back on. Without it, geometry past the far plane would be clamped instead of clipped. |
| `-Dnative_macos`: `geometryShader`, `fillModeNonSolid` and `depthClipEnable` become optional | fork only | KosmicKrisp provides every other DXVK requirement, including robustness2 `nullDescriptor`. D3D9 uses a GS only for `ProcessVertices` (SWVP emulation), which kisakcod never calls. Wireframe fill is for debug views only. |

## Verified (M1 Max, macOS 27, KosmicKrisp 26.2.4)

With an SDL3 window and a fixed-function quad:
- `CreateDevice` and every draw and `Present` succeed.
- A 4×4 green texture samples green, and a `ColorFill`-ed render-target texture samples correctly.
- `D3DTA_TFACTOR` renders the exact colour, and an unbound stage renders white.

The Khronos validation layer reports only DXVK's intentional aliasing of
`t2d`/`tcube` image variables at one binding, which is upstream behaviour on
every driver.

## MoltenVK (not used)

The earlier `kisakcod-moltenvk` branch made DXVK create a device and draw on
MoltenVK. It added portability enumeration, null-descriptor substitutes and a
1024 sampler cap, but texture sampling still read zero. Upstream does not
support portability-subset drivers, so that branch is kept only as a record.
