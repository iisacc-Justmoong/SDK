#pragma once
#include "WebSearch.h"

namespace iiGlobalSearch::detail {
std::shared_ptr<WebRenderer> browserRenderer(const WebConfiguration& config);
}
