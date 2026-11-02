# iiVolumetricSolidMesh

iiVolumetricSolidMesh is a Qt-free C++23 SDK for a hybrid 3D object: an
immutable CAD design snapshot, a derived tetrahedral volume and triangle
surface, and material particles representing non-overlapping tetrahedral cells. Applications can inspect the
surface as a mesh and use separate nodal or particle simulation states without
modifying the CAD source.

The first native authoring and meshing path is a dimensioned box. Custom CAD
kernels and tetrahedral meshers are attached through independent interfaces;
no other iiSDK module is a dependency. Materials and their spatial regions
are separate. Reference mass, center of mass, and centroidal inertia are
integrated over the piecewise-uniform tetrahedra, while particle inertia is
reported separately as a point-mass approximation. A material-only snapshot
update recalculates mass without remeshing or resampling the existing cells.

The SDK supplies FEM and particle-solver interfaces, not FEM, MPM, collision,
fracture, or rendering solvers. General B-Rep/NURBS/CSG editing and arbitrary
solid tetrahedralization require application-supplied implementations. The
application is responsible for supplying a non-overlapping, conforming volume
mesh and preserving or publishing files through its file provider.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Installation can be requested explicitly with `./install.sh` or an alternative
prefix as its first argument. See the [architecture and contracts](docs/architecture.md)
for units, error handling, numerical assumptions, sample generation, solver
boundaries, and the integration example.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
