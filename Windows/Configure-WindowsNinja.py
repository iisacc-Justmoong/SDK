"""Match Ninja's MSVC dependency prefix to the installed Korean compiler UI."""
import json, os, re, shutil, subprocess, sys
from pathlib import Path
workspace=Path(__file__).resolve().parents[2]
build=Path(sys.argv[1]).resolve()
if not build.is_relative_to(workspace) or build.name!='build':
    raise SystemExit('Expected a workspace build/ directory')
compiler=Path(shutil.which('cl.exe') or '')
rules=build/'CMakeFiles/rules.ninja'
if not rules.exists() or b'deps = msvc' not in rules.read_bytes():raise SystemExit(0)
probe=build/'.msvc-prefix-probe.cpp'
header=build/'.msvc-prefix-probe.h'
header.write_text('// dependency encoding probe\n',encoding='ascii')
probe.write_text('#include ".msvc-prefix-probe.h"\n',encoding='ascii')
obj=build/'.msvc-prefix-probe.obj'
sample=subprocess.run([str(compiler),'/nologo','/showIncludes','/c','/TP',str(probe),'/Fo'+str(obj)],capture_output=True,timeout=60)
prefix=None
for candidate in [b'Note: including file: ','참고: 포함 파일: '.encode('utf-8'),'참고: 포함 파일: '.encode('cp949')]:
    if candidate in sample.stdout or candidate in sample.stderr:
        prefix=candidate;break
probe.unlink(missing_ok=True);header.unlink(missing_ok=True);obj.unlink(missing_ok=True)
if prefix is None:raise SystemExit('Could not detect the MSVC include prefix')
data=rules.read_bytes()
data=re.sub(br'(?m)^ *msvc_deps_prefix = [^\r\n]*\r?\n',b'',data)
data=re.sub(br'(?m)^( +)deps = msvc\r?$',lambda m:m.group(0)+b'\n'+m.group(1)+b'msvc_deps_prefix = '+prefix,data)
rules.write_bytes(data)
receipt=build/'.windows-msvc-dependencies.json'
prior=json.loads(receipt.read_text()) if receipt.exists() else {}
if prior.get('prefix')!=prefix.hex() or prior.get('schema')!=2:
    (build/'.ninja_deps').unlink(missing_ok=True)
receipt.write_text(json.dumps({'schema':2,'compiler':str(compiler),'prefix':prefix.hex()},indent=2),encoding='utf-8')
print('Ninja MSVC header tracking configured from compiler output.')
