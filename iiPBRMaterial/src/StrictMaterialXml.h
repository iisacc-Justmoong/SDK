#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ii::pbr::exchange::detail {
// A preflight failure owns its diagnostic. It does not expose an XML tree.
struct StrictXmlError {
    std::string code;
    std::string message;
};

// Conservatively check sibling name uniqueness before MaterialX can discard a
// duplicate. DTDs and escaped or whitespace-normalized name values are rejected
// rather than guessed. This is not a general XML validator; MaterialX performs
// parsing afterwards. The source is borrowed only during the call; no I/O occurs.
[[nodiscard]] std::optional<StrictXmlError> checkStrictMaterialXml(std::string_view xml);
} // namespace ii::pbr::exchange::detail
