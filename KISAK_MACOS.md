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
| `getExePath` via `_NSGetExecutablePath`, `libvulkan.1.dylib` loader names | upstream PR doitsujin/dxvk#6000 | `getExePath` fell off the end of the function and trapped, and the loader was not found. Upstream will see this again only once DXVK runs on a macOS driver without fork-only relaxations. |
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

The earlier `kisakcod-moltenvk` work (now the tag `archive/kisakcod-moltenvk`) made DXVK create a device and draw on
MoltenVK. It added portability enumeration, null-descriptor substitutes and a
1024 sampler cap, but texture sampling still read zero. Upstream does not
support portability-subset drivers, so it is kept only as a record.

## Maintaining the branch

Patch order on top of upstream `master` (oldest first):

1. `[native] Fix building DXVK Native on macOS`: upstream PR #5962. Drop it once that merges.
2. `[native] Find the executable and the Vulkan loader on macOS`: `getExePath` and the dylib loader names: upstream PR #6000. Drop it once that merges.
3. `[dxvk] Clip depth through depth clamp when VK_EXT_depth_clip_enable is absent`: upstreamable.
4. `[kisakcod] Add -Dnative_macos`: fork only.
5. This document.

To rebase onto a new upstream release:

```
git fetch upstream
git rebase --onto <new-upstream-tag-or-master> <old-base> kisakcod-macos
```

Then re-check each patch:

- **Build:** run the `meson setup` command above, then `ninja`. If upstream adds a new `--version-script` site or another `__unix__`-only shim, extend patch 1 the same way.
- **Requirements:** if upstream adds a required device feature, query it on KosmicKrisp. If it's missing and D3D9 doesn't use it, add it to the `DXVK_MACOS_REQUIRED` list in patch 4.
- **Run:** a small D3D9 program (SDL3 window, fixed-function quad) must create its device, draw a sampled texture in the right colour, and present on KosmicKrisp. Run it under the Khronos validation layer as well. The only expected report is DXVK's intentional `t2d`/`tcube` binding aliasing.

Then push the branch and bump the `GIT_TAG` SHA in kisakcod's `scripts/extern/dxvk_macos.cmake`, in a kisakcod PR that states the run evidence.
