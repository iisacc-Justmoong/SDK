#pragma once

#include <expected>
#include <string>

namespace ii::webgpu {
enum class ErrorCode {
    InvalidArgument, BudgetExceeded, Exhausted, Unsupported, Unavailable,
    Validation, OutOfMemory, Internal, DeviceLost, Cancelled, NotReady, Timeout
};
struct Error final {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
};
template<class T> using Result = std::expected<T, Error>;
using Status = Result<void>;
}
