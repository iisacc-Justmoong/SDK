import hashlib
import re
import sys
from pathlib import Path

root=Path(sys.argv[1])
headers=[root/'src/iiGPU/GPU.h',root/'src/iiGPU/Completion.h']
for path in headers:
    if not path.exists(): continue
    text=path.read_text()
    assert not re.search(r'#\s*(?:include|import)\s*[<"](?:Q[A-Z]|Qt|vulkan/|Metal/|Foundation/)',text),path
assert not (root/'include').exists(),'source headers must remain beside implementation'
source=root/'example/Acceleration/transform.comp'
assert hashlib.sha256(source.read_bytes()).hexdigest() in (root/'example/Acceleration/Shader.h').read_text()
assert 'vkQueueSubmit' in (root/'src/iiGPU/Vulkan.cpp').read_text()
assert '[command commit]' in (root/'src/iiGPU/Metal.mm').read_text()
print('public boundary and shader provenance passed')
