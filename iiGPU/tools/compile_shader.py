#!/usr/bin/env python3
"""Rebuild the checked-in example SPIR-V; intermediates stay in build/."""
import argparse
import hashlib
import struct
import subprocess
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--compiler',required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
source=root/'example/Acceleration/transform.comp'
output=root/'build/shaders/transform.spv'
output.parent.mkdir(parents=True,exist_ok=True)
subprocess.run([args.compiler,'-V','--target-env','vulkan1.1','-o',str(output),str(source)],check=True)
words=struct.unpack('<'+'I'*(output.stat().st_size//4),output.read_bytes())
lines=['// Generated from transform.comp; SHA-256: '+hashlib.sha256(source.read_bytes()).hexdigest(),
       '#pragma once','#include <cstdint>','namespace example {','inline constexpr std::uint32_t transform_spirv[]{']
for i in range(0,len(words),8):
    lines.append('    '+','.join(f'0x{v:08x}u' for v in words[i:i+8])+',')
lines.extend(['};','} // namespace example',''])
(root/'example/Acceleration/Shader.h').write_text('\n'.join(lines))
