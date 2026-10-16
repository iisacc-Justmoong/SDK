# Local brush and correction verification

Verified on 2026-10-14 on macOS arm64 with AppleClang 21.0.0.21000334. Implementation
is split between iiPaintEngine 0.4.2 and iiGraphicAdjust 0.5.1. There is no dependency
between these SDKs; this upper-level consumer composes public installed APIs.

## Implemented scope

| Owner | Functionality |
|---|---|
| iiPaintEngine/Core | Detached normalized float PixelMask storage and validation |
| iiPaintEngine/Brush | Local size/feather/flow/density/stroke-opacity/Add/Erase/AutoMask options, optional pressure size/opacity, recovered pure feather/radius math |
| iiPaintEngine/Application | Immediate pointer-to-mask preview, commit/cancel, dirty-rectangle pixel undo/redo |
| iiGraphicAdjust/LocalCorrections | Already-computed RGB correction through borrowed mask, amount/inversion, encoded/linear sRGB interpolation, original alpha/hiddenRGB preservation |
| iiGraphicAdjust/SampleFilters | Evidence-bound Average8/16/Float32 and integer Solarize8/16 sample kernels |

Existing painting density remains dab placement density. The new local mask's
density is an absolute Add coverage ceiling. Existing Photoshop-style tone/color
and Lightroom-style rendered correction functions retain their own independent
numeric contracts. Local point curves, Kelvin white balance, HSL, detail, grain,
and other same-sized correction results can now use the same mask composition.

## Results

| Check | Result |
|---|---|
| iiPaintEngine Release |45/45 passed |
| iiPaintEngine ASan/UBSan/float-cast-overflow |45/45 passed |
| iiGraphicAdjust Release |163/163 passed |
| iiGraphicAdjust ASan/UBSan |163/163 passed |
| Temporary installed-package consumer |3/3 passed |
| Actual host installed-package consumer |3/3 passed |
| Lightroom original numeric prototypes |21 vectors, output radius/weight/profile float64 bits match |
| Photoshop original arm64 kernel replay against new SDK |78 Average + 26 Solarize cases,1,730,775 active samples,1,291,286 assertions passed |
| Separate pinned C++ reconstruction comparison |170 cases,4,147,431 checks passed |
| Product compile language |iiPaintEngine 109 units / iiGraphicAdjust 59 units, all C++23 |
| Library sources |iiPaintEngine 64 / iiGraphicAdjust 30, all `.cpp` |
| Direct library dependencies |libc++ and libSystem only |

The example uses a synthetic 256x144 image. At float pixel precision it changed
14,046 selected pixels, preserved 22,818 unselected pixels exactly, preserved source
alpha, and left input storage unchanged. It wrote five actual PNG files and a
machine-readable result. The committed integration test additionally confirms
preview separation, cancel, undo/redo, Erase, AutoMask color-boundary restriction,
hiddenRGB protection, and rejection of invalid borrowed coverage.

## Source and original execution evidence

The completed
[Adobe feature audit](/Volumes/Storage/Workspace/Engineering/Projects/Adobe/evidence/sdk-local-processing/evidence-audit.md)
binds declarations to original bytecode/resource hashes and records unresolved
native boundaries. Six pure Lua prototypes were copied unchanged into a
controlled closure harness for the 21 numeric vectors; the module main and native
CameraRaw bindings were excluded. Source ranges/interpreter/oracle/vector hashes
were independently checked by the parent task.

The original Average/Solarize arm64 replay uses owned mappings of SHA-bound
binary copies and synthetic host records. The comparison calls the new exported
SDK functions through names-only inline wrappers; it does not link the workspace
reconstruction. Original applications and installed binaries are unchanged.
Original instructions are uninstrumented; SDK unit suites separately provide
sanitizer coverage. The 170 case C++ reconstruction comparison is a separate check.

## Host installation

Generated, tested CMake packages were published with same-directory atomic leaf
replacement to `~/.local/SDK/iiPaintEngine`, its existing `platforms/macos` mirror,
and `~/.local/SDK/iiGraphicAdjust`. All 242 published package-file/hash or symlink
checks passed. Older iiGraphicAdjust ABI libraries and unrelated files were
preserved. Existing packages were backed up under the workspace build tree.

The normal login shell globally prefers the older macOS SDK mirror through
`DYLD_LIBRARY_PATH`. That override initially loaded the old painting library;
Python's startup stripped those loader variables and therefore did not expose
the same environment. CTest now unsets inherited loader overrides only for this
consumer to prove the selected package independently. Global shell settings are
unchanged. The actual host and mirror were then updated and normal login-shell
execution succeeded, with both loaded library paths/UUIDs matching the new files:

| Library | Verified SHA-256 |
|---|---|
| iiPaintEngine host and mirror |`88a8a28b2020fd3aff3ef6e4bec6f0e1847a9bf22caba2a8704970c80bef8e4c` |
| iiGraphicAdjust 0.5.1 |`7a8c59ef5671dcfa6dbafbdb0ffdd3c70ef87ce564388104c966f758400a08f6` |

The painting package explicitly fixes its install name to `@rpath`. No app was
restarted. Source build, staged package, actual installed package, and normal
runtime evidence are recorded separately in
[verification.json](/Volumes/Storage/Workspace/SDK/build/local-photo-processing/verification.json).

## Limits

The new float feather/flow/density/opacity/RGB AutoMask and correction composition
are independent SDK models. The narrowly recovered numeric helpers and sample
kernels have stronger original-execution evidence; that evidence does not prove
complete Photoshop/Lightroom output parity. CameraRaw native processing, GPU/AI,
ABR/PSD edit compatibility, physical pen behavior, Windows/Linux/mobile builds,
and consumer product UI migration are not established by this work. No user
photo, catalog, account, or active app session was accessed.
