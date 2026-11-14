# SDK module view migration boundary

An SDK module's maintained user interface is declared in LVUI and mounted by
an application through the module's C++ presentation model. The application
owns the iiGUI host and connects any services through interfaces. The model
may use the iiSDKCore runtime; it does not gain an iiGUI or LVUI dependency.
Legacy Qt Quick views and their QML-only hosts belong under a `Legacy` directory.
The LVRS and LVRS-Svelte projects are themselves legacy systems and are not
rewritten by this migration.

The source inventory covers the entire maintained tree of each `ii*` module
and LVUI, including resources, examples, tests and untracked files. It omits
hidden directories, `Legacy`, generated builds, third-party trees and symbolic
links. QML compatibility fixtures also belong under a `Legacy` directory. The
check does not inspect generated builds, installed packages, or application
repositories, and a clean inventory does not prove that an app mounts its
LVUI replacement. Run it with:

```sh
python3 -B tools/audit_module_views.py
python3 -B tests/test_module_views.py
```

The replacements are iiLicenseManager's license gate, iiAccountManager's
account flow, iiPhotoLibrary's gallery and iiMultimodalAgent's question sheet.
Each module's paired view documentation describes its service adapter, tests,
and remaining feature and platform boundaries. Society and Dreamscapes still
contain consumers of isolated QML views; their native host migration is not
implied by the module source inventory. Dreamscapes has macOS native question
and license gate integrations, and Society has native question and account
integrations in source, all with legacy fallbacks. Other module views, other platforms and
actual window display remain separate work.
