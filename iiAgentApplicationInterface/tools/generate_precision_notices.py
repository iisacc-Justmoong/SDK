#!/usr/bin/env python3
"""Collect license notices for the exact validator's locked Cargo packages.

Run `cargo fetch --locked --manifest-path src/PrecisionSchema/Cargo.toml`
first. The output documents all locked packages, including target-specific
ones, so installed binaries retain the authors' license notices.
"""
from pathlib import Path
import os
import tomllib

root = Path(__file__).resolve().parents[1]
manifest = root / 'src/PrecisionSchema'
cargo_home = Path(os.environ.get('CARGO_HOME', Path.home() / '.cargo'))
registries = list((cargo_home / 'registry/src').glob('*'))
packages = tomllib.loads((manifest / 'Cargo.lock').read_text())['package']
sections = ['Exact JSON Schema backend: third-party package notices\n'
            'Packages offering multiple licenses are consumed under a permissive alternative.\n']
for package in packages:
    if 'source' not in package:
        continue
    name, version = package['name'], package['version']
    directory = next((base / f'{name}-{version}' for base in registries if (base / f'{name}-{version}').is_dir()), None)
    if directory is None:
        raise SystemExit(f'Missing Cargo source for {name} {version}; run cargo fetch --locked')
    metadata = tomllib.loads((directory / 'Cargo.toml').read_text())['package']
    files = sorted(p for p in directory.iterdir() if p.is_file() and
                   p.name.lower().startswith(('license', 'copying', 'notice', 'unlicense')))
    if not files and name.startswith('jsonschema-'):
        files = [manifest / 'vendor-notices/jsonschema-LICENSE']
    if not files and name in ('uuid-simd', 'vsimd'):
        files = [manifest / 'vendor-notices/simd-LICENSE']
    if not files and name == 'r-efi':
        files = [directory / 'AUTHORS']
    if not files:
        raise SystemExit(f'No license notice found for {name} {version}')
    sections.append(f'\n==== {name} {version} ====\nLicense: {metadata.get("license", "see notice")}\n'
                    f'Source: {metadata.get("repository", package["source"])}\n'
                    f'Cargo checksum: {package.get("checksum", "")}\n')
    for file in files:
        sections.append(f'\n--- {file.name} ---\n{file.read_text(encoding="utf-8", errors="replace").strip()}\n')
(manifest / 'THIRD-PARTY-NOTICES.txt').write_text(''.join(sections), encoding='utf-8')
