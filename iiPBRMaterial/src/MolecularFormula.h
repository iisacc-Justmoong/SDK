#pragma once
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ii::pbr {
class CustomAtomType;
enum class ErrorCode { invalid_input, limit_exceeded, unsupported, not_found, ambiguous, backend_failure };
struct Error { ErrorCode code; std::string message; };
template<class T> using Result = std::expected<T, Error>;

struct MolecularFormula {
    std::string input;
    std::string canonical; // Hill order for known elements, then custom IDs; no reduction
    std::map<std::string,std::uint64_t> atoms;
    std::optional<double> molarMassGramsPerMol; // absent when a weight is unavailable
};
[[nodiscard]] bool isKnownElementSymbol(std::string_view symbol) noexcept;
// Supports element symbols, ASCII or Unicode-subscript counts, nested ()/[], and integer hydrate/adduct terms
// separated by . or U+00B7. Isotopes, charge, fractional counts and polymers
// are deliberately rejected, not guessed. Atom weights are a limited table.
[[nodiscard]] Result<MolecularFormula> parseMolecularFormula(std::string_view);
// Custom atom references use {id} and must be supplied explicitly.
[[nodiscard]] Result<MolecularFormula> parseMolecularFormula(std::string_view,std::span<const CustomAtomType>);
}
