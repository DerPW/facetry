#pragma once

#include <stdexcept>
#include <string>

namespace facetry::core {

// Values match facetry_status in facetry_core.h.
enum class ErrorCode {
    InvalidArgument = 1,
    InvalidHandle = 2,
    Kernel = 3,
    Unsupported = 4,
    Io = 5,
    OutOfMemory = 6,
    Internal = 7,
};

// The only exception type facetry::core functions throw on purpose. The C API turns it into a
// status code and message; nothing is thrown across that boundary.
class Error : public std::runtime_error {
  public:
    Error(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept {
        return code_;
    }

  private:
    ErrorCode code_;
};

}  // namespace facetry::core
