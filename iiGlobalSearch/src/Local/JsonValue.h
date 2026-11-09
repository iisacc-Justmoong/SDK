#pragma once

#include <iiSDKCore/Core/Value/Value.h>
#include <optional>
#include <string_view>

namespace iiGlobalSearch::detail {
[[nodiscard]]
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((visibility("hidden")))
#endif
std::optional<ii::core::Value> parseCoreJson(std::string_view text);
}
