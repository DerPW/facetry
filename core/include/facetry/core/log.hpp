#pragma once

#include <functional>
#include <string_view>

namespace facetry::core {

// Values match facetry_log_level in facetry_core.h.
enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warning = 3, Error = 4 };

using LogSink = std::function<void(LogLevel level, std::string_view message)>;

// Process-wide. An empty sink restores the default (standard error).
void setLogSink(LogSink sink);
// Messages below this level are dropped. Default: Info.
void setLogLevel(LogLevel level) noexcept;
[[nodiscard]] bool isLogEnabled(LogLevel level) noexcept;

void log(LogLevel level, std::string_view message);

}  // namespace facetry::core
