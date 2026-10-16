// JSON form of the document model, shared by save/load and the regeneration cache.
#pragma once

#include "facetry/core/document.hpp"
#include "facetry/core/error.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace facetry::core::json {

// Keeps keys in the order they are written, so saved documents read naturally ("id" first).
using Json = nlohmann::ordered_json;

[[noreturn]] inline void invalid(const std::string& message) {
    throw Error(ErrorCode::InvalidArgument, "Invalid document: " + message);
}

// Reading: every accessor checks the type and names the place of a problem.
class Reader {
  public:
    Reader(const Json& value, std::string path) : value_(value), path_(std::move(path)) {}

    [[nodiscard]] Reader at(const char* key) const {
        if (!value_.is_object() || !value_.contains(key)) {
            invalid(path_ + ": missing \"" + key + "\"");
        }
        return {value_.at(key), path_ + "." + key};
    }
    [[nodiscard]] bool has(const char* key) const {
        return value_.is_object() && value_.contains(key) && !value_.at(key).is_null();
    }
    [[nodiscard]] std::vector<Reader> items() const {
        if (!value_.is_array()) {
            invalid(path_ + " must be an array");
        }
        std::vector<Reader> result;
        for (std::size_t i = 0; i < value_.size(); ++i) {
            result.emplace_back(value_.at(i), path_ + "[" + std::to_string(i) + "]");
        }
        return result;
    }
    [[nodiscard]] std::string string() const {
        if (!value_.is_string()) {
            invalid(path_ + " must be a string");
        }
        return value_.get<std::string>();
    }
    [[nodiscard]] double number() const {
        if (!value_.is_number()) {
            invalid(path_ + " must be a number");
        }
        return value_.get<double>();
    }
    [[nodiscard]] std::uint64_t unsignedNumber() const {
        if (!value_.is_number_unsigned()) {
            invalid(path_ + " must be a non-negative integer");
        }
        return value_.get<std::uint64_t>();
    }
    [[nodiscard]] bool boolean() const {
        if (!value_.is_boolean()) {
            invalid(path_ + " must be true or false");
        }
        return value_.get<bool>();
    }
    [[nodiscard]] Value value() const {
        if (value_.is_string()) {
            return Value::of(value_.get<std::string>());
        }
        return {number()};
    }
    [[nodiscard]] Vec3 vec() const {
        const auto list = items();
        if (list.size() != 3) {
            invalid(path_ + " must have three numbers");
        }
        return {list[0].number(), list[1].number(), list[2].number()};
    }
    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

  private:
    const Json& value_;
    std::string path_;
};

[[nodiscard]] Json featureToJson(const Feature& feature);
// Reads a feature written by featureToJson(). `path` names it in error messages.
[[nodiscard]] Feature featureFromJson(const Json& value, const std::string& path);
[[nodiscard]] Json referenceJson(const Reference& reference);

}  // namespace facetry::core::json
