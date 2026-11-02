# iiPBRMaterial

iiPBRMaterial is an independent, Qt-free C++23 material module. It parses
molecular formulas, resolves explicitly identified and sourced physical
material records, samples metallic-roughness PBR textures, and maps categorical
texture colors to material IDs. It performs no file I/O; an application passes
texture bytes through a decoder or supplies already decoded RGBA pixels.
Explicit atom and bond graphs can also be turned into renderer-independent 2D
molecular maps and in-memory SVG; the formula alone never invents connectivity.
Molecules accept composition-matched alternative structures for explicit map
and material selection, without certifying chemical isomerism or material identity.
Organic materials additionally expose user-defined biological composition,
properties, state, replaceable dynamics, and explicit optical coefficients.
Custom atom types have explicit identities, optional physical data, and
namespaced formula notation; they remain separate from known chemical elements.
Custom bonds can define endpoint constraints, direction, energy, mechanical
parameters, visualization, and replaceable distance-dependent potentials.

The reference catalog is intentionally small. A formula describes composition,
not a unique phase, structure, density or engineering material. Catalog
selection and physical reference conditions must be checked by the caller.
Texture colors likewise require an explicit material-ID palette; appearance
alone does not establish composition. The application composes this module with
other iiSDK modules using their separate APIs.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`./install.sh` installs the package when explicitly invoked. See
[the material and texture contracts](docs/materials.md) for supported formula
notation, sources and units, texture decoding, physical limitations, and the
volumetric composition example.
See [the molecular map guide](docs/molecular-map.md) for alternative structures
and the composition-only validation contract.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
