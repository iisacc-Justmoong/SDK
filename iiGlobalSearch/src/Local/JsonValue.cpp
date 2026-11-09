#include "JsonValue.h"
#include <iiJson/Json.h>
#include <type_traits>
#include <variant>

namespace iiGlobalSearch::detail {
namespace {
ii::core::Value toCore(const ii::json::Value& source) {
    return std::visit([](const auto& value) -> ii::core::Value {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) return {};
        else if constexpr (std::is_same_v<T, ii::json::Value::Array>) {
            ii::core::Value::Array result;
            result.reserve(value.size());
            for (const auto& item : value) result.push_back(toCore(item));
            return ii::core::Value(std::move(result));
        } else if constexpr (std::is_same_v<T, ii::json::Value::Object>) {
            ii::core::Value::Object result;
            for (const auto& [key, item] : value) result.emplace(key, toCore(item));
            return ii::core::Value(std::move(result));
        } else return ii::core::Value(value);
    }, source.storage());
}
}

std::optional<ii::core::Value> parseCoreJson(std::string_view text) {
    const auto parsed = ii::json::parse(text);
    if (!parsed) return {};
    return toCore(*parsed);
}
}
