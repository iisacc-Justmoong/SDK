# iiLocalizer

iiLocalizer is an independent C++23 iiSDK library for looking up messages in
UTF-8 GNU gettext PO catalogs and substituting positional arguments. It also
provides a C ABI and Python API. The first implementation handles singular
messages and exact optional contexts; plural entries are explicitly rejected.
The broader localization design remains a target, not a claim that every
feature is implemented.

A separate application-owned example connects a translated message to an LVUI
`Label` through the existing `AppStrings` interface. Label usage selection
accepts an optional exact application location; omitting it selects every
registered occurrence of the source message. The library itself does not link
LVUI, iiGUI, Qt, KDE, or another iiSDK module.

```cpp
#include <iiLocalizer>
#include <array>

const auto catalog = ii::localizer::Catalog::parse(po_text);
const std::array<std::string, 1> arguments{"Jimin"};
if (catalog) {
    const auto result = catalog->translate("Hello, %1", arguments);
    // result->text contains the translated and substituted label text.
}
```

Build and run the module's automatic tests with `cmake -S . -B build`,
`cmake --build build`, and `ctest --test-dir build --output-on-failure`.
`IILOCALIZER_BUILD_TESTS` controls its tests; consumers may link
`iiLocalizer::iiLocalizer` or use the installed `iilocalizer_link` CMake helper.

- [Documentation index](docs/README.md)
- [Implemented API, limitations, and LVUI consumer verification](docs/Implementation.md)
- [Localization contract](docs/LocalizationContract.md)
- [PO workflow](docs/POWorkflow.md)
- [Agent instructions](AGENT.md)
