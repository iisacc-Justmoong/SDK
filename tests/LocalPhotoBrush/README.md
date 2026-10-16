# Local photo correction consumer

This C++23 consumer links the separately installed `iiPaintEngine 0.4.2` and
`iiGraphicAdjust 0.5.1` packages. The modules do not depend on one another.
The application owns the composition: pointer input makes a float pixel mask;
the same borrowed coverage localizes white balance, light, curves, color,
detail, and grain processing. Public API inputs are sufficient; there are no
private includes or links to Qt, Adobe, or a scripting runtime.

```sh
cmake -S tests/LocalPhotoBrush -B build/local-photo-processing/consumer \
  -DCMAKE_PREFIX_PATH="$HOME/.local/SDK/iiPaintEngine;$HOME/.local/SDK/iiGraphicAdjust" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/local-photo-processing/consumer --parallel 2
ctest --test-dir build/local-photo-processing/consumer --output-on-failure
build/local-photo-processing/consumer/LocalPhotoBrush build/local-photo-processing/images
```

Commands assume the parent `SDK` directory. All output stays under `build/`.
The example generates a synthetic 256x144 image and writes source, mask, full
correction, local correction, and a three-panel comparison as PNG. `result.json`
reports actual changed pixels, exact preservation outside the mask, and source
and alpha preservation. It uses no user photograph, account, Adobe GUI, or model.

The integration test verifies preview/commit/cancel and pixel undo/redo across
the two packages, strong color-boundary Auto Mask, alpha-zero source handling,
fractional correction strength, detached output, and failure on invalid masks.

An optional source comparison accepts
`-DPHOTOSHOP_RECONSTRUCTION_ROOT=/Volumes/Storage/Workspace/Engineering/Projects/Adobe/Photoshop`.
It builds the workspace's pinned, independently authored Average/Solarize
reconstruction only into a test executable, and compares every Solarize integer
input plus tiled/strided selected Average cases to the installed SDK. This is
separate from the previous original-instruction replay. It does not run Adobe
instructions or prove Adobe application pixel parity for the new local brush.

On macOS, CTest unsets inherited `DYLD_LIBRARY_PATH`/`DYLD_FRAMEWORK_PATH` for
these tests only, so a login shell's older SDK overrides do not replace the
selected package's rpaths. It does not modify global shell settings. Actual
host installation is also validated with the normal login environment and the
existing macOS package mirror.

See [verification and evidence boundaries](Verification.md) for the completed
module, original-kernel, staged-package, actual-installation, and runtime checks.
