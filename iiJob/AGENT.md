# iiJob agent instructions

- Keep public APIs C++23, Qt-free and independent of other SDK modules.
- The application owns authorization, worker lifetime and database-file creation.
- Never retry an unknown side effect until the host has verified its outcome.
- Preserve successful items and attempt history when retrying failures.
- Put builds under `build/` and temporary test resources under `resources/`.
- Update the paired Markdown and agent JSON when changing documentation.
