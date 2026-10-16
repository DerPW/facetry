// facetry-cli: builds models from JSON descriptions without any UI. Used for tests and debugging.
#include "facetry/core/error.hpp"
#include "facetry/core/mesh.hpp"
#include "facetry/core/stl.hpp"
#include "facetry/core/version.hpp"
#include "facetry/script/model_script.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using facetry::core::Error;

constexpr std::string_view kUsage = R"(Usage:
  facetry-cli version
  facetry-cli info <model.json> [--stl <out.stl>] [--deflection <mm>]
  facetry-cli check <model.json | directory>...

  info   Builds the model and prints its properties and mesh statistics as JSON.
  check  Builds each model and compares it with its "expected" section. Exits with 1 on any
         mismatch.
)";

std::string readFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw Error(facetry::core::ErrorCode::Io, "Cannot read " + path.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

int version() {
    std::cout << "facetry-cli " << facetry::core::version() << " (OpenCASCADE "
              << facetry::core::kernelVersion() << ", " << facetry::core::platform() << ")\n";
    return EXIT_SUCCESS;
}

int info(const std::vector<std::string_view>& args) {
    if (args.empty()) {
        std::cerr << kUsage;
        return 2;
    }
    fs::path stlPath;
    facetry::core::TessellationOptions options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--stl" && i + 1 < args.size()) {
            stlPath = std::string(args[++i]);
        } else if (args[i] == "--deflection" && i + 1 < args.size()) {
            options.linearDeflection = std::stod(std::string(args[++i]));
        } else {
            std::cerr << "Unknown option: " << args[i] << "\n" << kUsage;
            return 2;
        }
    }

    const auto shape = facetry::script::buildModel(readFile(std::string(args[0])));
    const auto props = shape.properties();
    const auto mesh = facetry::core::tessellate(shape, options);

    const nlohmann::ordered_json report = {
        {"volume", props.volume},
        {"area", props.area},
        {"bbox",
         {{props.bounds.min.x, props.bounds.min.y, props.bounds.min.z},
          {props.bounds.max.x, props.bounds.max.y, props.bounds.max.z}}},
        {"solids", props.counts.solids},
        {"faces", props.counts.faces},
        {"edges", props.counts.edges},
        {"vertices", props.counts.vertices},
        {"mesh",
         {{"triangles", mesh.triangleCount()},
          {"vertices", mesh.meshVertexCount()},
          {"edgePolylines", mesh.edgeCount()},
          {"linearDeflection", options.linearDeflection}}},
    };
    std::cout << report.dump(2) << "\n";

    if (!stlPath.empty()) {
        std::ofstream out(stlPath, std::ios::binary);
        facetry::core::writeBinaryStl(mesh, out);
        std::cerr << "Wrote " << stlPath.string() << "\n";
    }
    return EXIT_SUCCESS;
}

int check(const std::vector<std::string_view>& args) {
    std::vector<fs::path> files;
    for (const auto arg : args) {
        const fs::path path{std::string(arg)};
        if (fs::is_directory(path)) {
            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.path().extension() == ".json") {
                    files.push_back(entry.path());
                }
            }
        } else {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "No model files given\n" << kUsage;
        return 2;
    }

    int failures = 0;
    for (const auto& file : files) {
        try {
            const std::string json = readFile(file);
            const auto mismatches =
                facetry::script::checkExpected(facetry::script::buildModel(json), json);
            if (mismatches.empty()) {
                std::cout << "ok    " << file.filename().string() << "\n";
            } else {
                ++failures;
                std::cout << "FAIL  " << file.filename().string() << "\n";
                for (const auto& mismatch : mismatches) {
                    std::cout << "        " << mismatch << "\n";
                }
            }
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "ERROR " << file.filename().string() << ": " << error.what() << "\n";
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::cerr << kUsage;
        return 2;
    }
    const std::vector<std::string_view> rest(args.begin() + 1, args.end());
    try {
        if (args[0] == "version") {
            return version();
        }
        if (args[0] == "info") {
            return info(rest);
        }
        if (args[0] == "check") {
            return check(rest);
        }
        std::cerr << kUsage;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
