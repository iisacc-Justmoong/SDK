# iiJob

iiJob is a C++23, Qt-free SDK module for durable, itemized jobs. It records
requests, per-item attempts, checkpoints and outputs in an existing SQLite
database. Applications own execution, file creation, authorization and the
exclusive lease used to recover a stopped worker. The module depends on SQLite
and the C++ standard library, not on another iiSDK module.

Use `ii::job::Store` with a regular, pre-created database file; a host such as
Society creates it through iiFileProvider before opening it. See
[job storage and recovery](docs/Jobs.md) for the lifecycle and guarantees.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests use the installed iiFileProvider Python adapter to create disposable
fixtures under `resources/`; this is a test-host dependency, not a library or
public API dependency. `install.sh` installs the package under the chosen prefix.
