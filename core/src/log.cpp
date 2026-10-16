#include "facetry/core/log.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>

namespace facetry::core {
namespace {

std::mutex& sinkMutex() {
    static std::mutex mutex;
    return mutex;
}

LogSink& sink() {
    static LogSink instance;
    return instance;
}

std::atomic<int>& minimumLevel() {
    static std::atomic<int> level{static_cast<int>(LogLevel::Info)};
    return level;
}

void writeToStandardError(LogLevel level, std::string_view message) {
    static constexpr std::array<const char*, 5> names = {"trace", "debug", "info", "warning",
                                                         "error"};
    const auto index = static_cast<std::size_t>(level);
    const std::string line =
        std::string("[facetry] ") + names.at(index) + ": " + std::string(message) + "\n";
    std::fputs(line.c_str(), stderr);
}

}  // namespace

void setLogSink(LogSink newSink) {
    const std::lock_guard lock(sinkMutex());
    sink() = std::move(newSink);
}

void setLogLevel(LogLevel level) noexcept {
    minimumLevel().store(static_cast<int>(level));
}

bool isLogEnabled(LogLevel level) noexcept {
    return static_cast<int>(level) >= minimumLevel().load();
}

void log(LogLevel level, std::string_view message) {
    if (!isLogEnabled(level)) {
        return;
    }
    const std::lock_guard lock(sinkMutex());
    if (sink()) {
        sink()(level, message);
    } else {
        writeToStandardError(level, message);
    }
}

}  // namespace facetry::core
