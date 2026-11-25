#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ii::localizer {

/// Identifies whether the final text came from the catalog or a source fallback.
enum class Origin { translated, source_missing, source_invalid_translation };

/// Carries a human-readable failure and, for PO syntax errors, a one-based line.
struct Error final {
    std::string message;
    std::size_t line = 0;
};

/// Returns the fully formatted text and explains the translation fallback used.
struct Translation final {
    std::string text;
    Origin origin = Origin::source_missing;
};

/// Associates an exact application-owned location and source message with one string key.
/// Each location and key must be unique across the complete supplied registry.
struct LabelUsage final {
    std::string location;
    std::string source;
    std::string key;
};

/// Return input indices whose source and optional location match exactly, in input order.
/// Omitting location selects every registered use of source; a missing match returns an
/// empty vector. Empty fields, malformed UTF-8, duplicate locations or keys and resource
/// limits produce an error. The caller owns the registry and its mapping to real labels.
[[nodiscard]] std::expected<std::vector<std::size_t>, Error> select_label_usages(
    std::span<const LabelUsage> uses, std::string_view source,
    std::optional<std::string_view> location = std::nullopt);

/// Immutable, independently owned UTF-8 PO messages; safe to read concurrently.
class Catalog final {
public:
    /// Parse a complete PO input atomically, returning a diagnostic on failure.
    [[nodiscard]] static std::expected<Catalog, Error> parse(std::string_view bytes);

    /// Return the optional Language header, or an empty string when it is absent.
    [[nodiscard]] std::string_view language() const noexcept;

    /// Translate an exact source/context pair, then substitute numbered text arguments.
    /// A missing context differs from an explicitly present empty context.
    [[nodiscard]] std::expected<Translation, Error> translate(
        std::string_view source, std::span<const std::string> arguments = {},
        std::optional<std::string_view> context = std::nullopt) const;

private:
    struct Data;
    explicit Catalog(std::shared_ptr<const Data> data) noexcept;
    std::shared_ptr<const Data> data_;
};

} // namespace ii::localizer
