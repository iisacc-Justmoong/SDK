#pragma once
#include <optional>
#include <string_view>

namespace iiAgentApplicationInterface::detail {
/// Resolve immutable, embedded standard legacy meta-schemas without I/O.
/// The returned view is process-lifetime read-only data and carries no authority.
[[nodiscard]] std::optional<std::string_view> legacy_meta(std::string_view uri) noexcept;
}
