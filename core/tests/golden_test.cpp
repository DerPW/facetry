// Regenerates every benchmark model in tests/models and compares the result with the values
// stored in the model file (see tests/models/README.md).
#include "facetry/script/model_script.hpp"

#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::filesystem::path> modelFiles() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(FACETRY_MODELS_DIR)) {
        if (entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("golden models regenerate to their expected results") {
    const auto files = modelFiles();
    REQUIRE_FALSE(files.empty());
    for (const auto& path : files) {
        CAPTURE(path.filename().string());
        const std::string json = readFile(path);
        const auto shape = facetry::script::buildModel(json);
        const auto mismatches = facetry::script::checkExpected(shape, json);
        for (const auto& mismatch : mismatches) {
            FAIL_CHECK(mismatch);
        }
    }
}
