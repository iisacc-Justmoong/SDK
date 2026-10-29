# iiCompression example

```cpp
#include <iiCompression/Gzip.h>

ii::compression::Limits limits;
limits.input_bytes = 1024 * 1024;
limits.output_bytes = 8 * 1024 * 1024;
auto decoded = ii::compression::gzip_decompress(compressedBytes, limits);
if (!decoded) {
    // Report decoded.error(); no partial output is available.
}
```

Applications own the input buffer, file I/O, and composition with other SDKs.
