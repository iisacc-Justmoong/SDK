import hashlib
import ast
import re
import sys
from pathlib import Path

root=Path(sys.argv[1])
headers=[root/'src/iiGPU/GPU.h',root/'src/iiGPU/Completion.h']
for path in headers:
    if not path.exists(): continue
    text=path.read_text()
    assert not re.search(r'#\s*(?:include|import)\s*[<"](?:Q[A-Z]|Qt|vulkan/|Metal/|Foundation/)',text),path
for path in (root/'src/iiGPU').rglob('*'):
    if path.suffix not in ('.h','.cpp','.mm'): continue
    assert not re.search(r'#\s*(?:include|import)\s*[<"]iiSDKCore/',path.read_text()),path
for path in (root/'CMakeLists.txt',root/'cmake/iiGPUConfig.cmake.in'):
    text=path.read_text()
    assert not re.search(r'find_(?:package|dependency)\s*\(\s*iiSDKCore\b|iiSDKCore::',text),path
bootstrap=ast.parse((root/'tools/fetch_dawn.py').read_text())
for node in ast.walk(bootstrap):
    if isinstance(node,ast.Import):
        assert all(not alias.name.startswith('ii_file_provider') for alias in node.names)
    if isinstance(node,ast.ImportFrom) and node.module:
        assert not node.module.startswith('ii_file_provider')
assert not (root/'include').exists(),'source headers must remain beside implementation'
source=root/'example/Acceleration/transform.comp'
assert hashlib.sha256(source.read_bytes()).hexdigest() in (root/'example/Acceleration/Shader.h').read_text()
assert 'vkQueueSubmit' in (root/'src/iiGPU/Vulkan.cpp').read_text()
assert '[command commit]' in (root/'src/iiGPU/Metal.mm').read_text()
print('public boundary and shader provenance passed')
