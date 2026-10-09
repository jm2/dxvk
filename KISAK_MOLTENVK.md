# kisakcod MoltenVK profile (jm2/dxvk fork)

This branch (`kisakcod-moltenvk`) carries the patches that kisakcod's macOS
client needs to run DXVK Native's D3D9 on Apple GPUs through MoltenVK
(Vulkan → Metal). It is pinned by kisakcod and built with:

```
meson setup build --buildtype=release -Dnative_sdl3=enabled \
  -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false \
  -Dnative_moltenvk=true
```

Without `-Dnative_moltenvk=true`, the result matches upstream plus the two
macOS PRs below.

## Patches

| Patch | Status | Why |
|---|---|---|
| Build on macOS: `__APPLE__` compat shims, `<cstddef>`, no GNU `--version-script` on darwin, `tzcnt` overload by width | upstream PR doitsujin/dxvk#5962 | DXVK Native did not compile on macOS. |
| Runtime on macOS: `getExePath` via `_NSGetExecutablePath`, `libvulkan.1.dylib` loader names, `VK_KHR_portability_enumeration` | upstream PR doitsujin/dxvk#5963 | `getExePath` trapped. The loader was not found. The loader hides MoltenVK unless the instance opts into portability enumeration. |
| `-Dnative_moltenvk`: `geometryShader`, `shaderCullDistance`, `depthClipEnable`, `robustBufferAccess2` and `nullDescriptor` become optional | fork only | MoltenVK cannot provide them. D3D9 uses a GS only for `ProcessVertices` (SWVP emulation), which kisakcod never calls. D3D9 does not use cull distance. |
| Enable `VK_KHR_portability_subset` when exposed | fork only | Required by the Vulkan spec on portability devices. |
| Depth clip without `VK_EXT_depth_clip_enable`: set `depthClampEnable = !depthClip` and do not chain the depth-clip struct | fork (upstreamable) | DXVK always enables depth clamp and relies on the extension to turn clipping back on. Without the extension, D3D9 geometry beyond the far plane would be clamped instead of clipped. |
| `MaxSamplerCount` 1024 under `-Dnative_moltenvk` | fork only | MoltenVK reports `maxPerStageDescriptorUpdateAfterBindSamplers = 1024`. DXVK's 2048-entry global sampler array exceeds that. |
| Null-descriptor substitutes: without `nullDescriptor` on the legacy descriptor path, unbound sampled images bind a black 1×1 image of the right view type. Unbound uniform, storage and texel buffers and vertex streams bind the zero-filled dummy buffer. | fork only | Writing `VK_NULL_HANDLE` is invalid usage without `nullDescriptor`. kisakcod unbinds samplers (`SetTexture(i, 0)`). Storage images stay null; D3D9 has no UAVs. |

## Known gaps

- **Open: texture sampling reads zero.** A probe (SDL3 window, fixed-function
  quad) on an M1 Max with MoltenVK 1.4.2 gives these results:
  - `CreateDevice` and every draw and `Present` succeed.
  - A `D3DTA_TFACTOR` quad renders the exact colour.
  - An unbound stage renders white.
  - Quads sampling a CPU-uploaded texture and a `ColorFill`-ed render-target
    texture both come out black.

  MoltenVK logs no shader-conversion errors, and its descriptor-indexing
  features are all reported. This still needs GPU-level debugging (Metal
  frame capture).
- Without these patches, the same probe segfaults, because null descriptors
  are invalid without `nullDescriptor`.

- Without `robustBufferAccess2`, out-of-bounds buffer reads return some
  in-bounds value instead of zero (core robustness only). Skinned models are
  the first thing to watch.
- D24S8 is emulated as D32_SFLOAT_S8 (Apple GPUs have no D24).
