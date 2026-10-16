// facetry-cli: builds models from JSON descriptions without any UI. Used for tests and debugging.
#include "facetry/core/error.hpp"
#include "facetry/core/file_format.hpp"
#include "facetry/core/mesh.hpp"
#include "facetry/core/session.hpp"
#include "facetry/core/stl.hpp"
#include "facetry/core/version.hpp"
#include "facetry/script/model_script.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <cstddef>
#else
#include <sys/resource.h>
#endif

namespace {

namespace fs = std::filesystem;
using facetry::core::Error;

constexpr std::string_view kUsage = R"(Usage:
  facetry-cli version
  facetry-cli info <model.json> [--stl <out.stl>] [--deflection <mm>]
  facetry-cli check <model.json | directory>...
  facetry-cli bench <document.json> [--runs <n>]
  facetry-cli unpack <file.facetry>
  facetry-cli pack <document.json> <file.facetry>

  info   Builds the model and prints its properties and mesh statistics as JSON.
  check  Builds each model and compares it with its "expected" section. Exits with 1 on any
         mismatch.
  bench  Loads a document (tests/documents) and times regeneration: from scratch, after a
         change to the first parameter, and with everything cached. Prints JSON.
  unpack Prints the document.json inside a .facetry file, e.g. to diff or version it as text.
  pack   Writes a document (document.json or any file `unpack` accepts) as a .facetry file.
)";

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
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

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    const std::string text = readFile(path);
    return {text.begin(), text.end()};
}

int unpack(const std::vector<std::string_view>& args) {
    if (args.size() != 1) {
        std::cerr << kUsage;
        return 2;
    }
    const auto contents = facetry::core::readFile(readBytes(std::string(args[0])));
    std::cout << facetry::core::toJson(contents.document);
    return EXIT_SUCCESS;
}

int pack(const std::vector<std::string_view>& args) {
    if (args.size() != 2) {
        std::cerr << kUsage;
        return 2;
    }
    const auto contents = facetry::core::readFile(readBytes(std::string(args[0])));
    const auto bytes = facetry::core::writeFile(contents.document, contents.thumbnail);
    std::ofstream out(std::string(args[1]), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw Error(facetry::core::ErrorCode::Io, "Cannot write " + std::string(args[1]));
    }
    std::cerr << "Wrote " << args[1] << " (" << bytes.size() << " bytes)\n";
    return EXIT_SUCCESS;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// Peak memory of the process (native) or the size of the WebAssembly memory, which only grows.
double peakMemoryMegabytes() {
#ifdef __EMSCRIPTEN__
    return static_cast<double>(__builtin_wasm_memory_size(0)) * 65536.0 / 1e6;
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return static_cast<double>(usage.ru_maxrss) / 1e6;  // bytes
#else
    return static_cast<double>(usage.ru_maxrss) / 1e3;  // kilobytes
#endif
#endif
}

int bench(const std::vector<std::string_view>& args) {
    if (args.empty()) {
        std::cerr << kUsage;
        return 2;
    }
    int runs = 20;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--runs" && i + 1 < args.size()) {
            runs = std::max(1, std::stoi(std::string(args[++i])));
        } else {
            std::cerr << "Unknown option: " << args[i] << "\n" << kUsage;
            return 2;
        }
    }
    using facetry::core::Session;
    const std::string json = readFile(std::string(args[0]));
    std::vector<double> load;
    std::vector<double> full;
    std::vector<double> parameter;
    std::vector<double> cached;
    std::size_t features = 0;
    for (int run = 0; run < runs; ++run) {
        auto start = std::chrono::steady_clock::now();
        Session session = Session::load(json);
        load.push_back(millisecondsSince(start));

        start = std::chrono::steady_clock::now();
        const auto& regeneration = session.regenerate();
        full.push_back(millisecondsSince(start));
        features = regeneration.features.size();
        for (const auto& feature : regeneration.features) {
            if (feature.status == facetry::core::FeatureStatus::Error) {
                throw Error(facetry::core::ErrorCode::Kernel,
                            "Feature " + feature.feature + " failed: " +
                                (feature.messages.empty() ? "" : feature.messages.front()));
            }
        }

        if (!session.document().parameters.empty()) {
            session.edit([](facetry::core::Document& document) {
                document.parameters.front().value *= 1.01;
            });
            start = std::chrono::steady_clock::now();
            session.regenerate();
            parameter.push_back(millisecondsSince(start));
        }

        start = std::chrono::steady_clock::now();
        session.regenerate();
        cached.push_back(millisecondsSince(start));
    }

    nlohmann::ordered_json report = {
        {"document", fs::path(std::string(args[0])).filename().string()},
        {"platform", facetry::core::platform()},
        {"features", features},
        {"runs", runs},
        {"loadMs", median(load)},
        {"regenerateMs", median(full)},
        {"firstRegenerateMs", full.front()},
        {"cachedRegenerateMs", median(cached)},
        {"peakMemoryMB", peakMemoryMegabytes()},
    };
    if (!parameter.empty()) {
        report["firstParameterChangeMs"] = median(parameter);
    }
    std::cout << report.dump(2) << "\n";
    return EXIT_SUCCESS;
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
        if (args[0] == "bench") {
            return bench(rest);
        }
        if (args[0] == "unpack") {
            return unpack(rest);
        }
        if (args[0] == "pack") {
            return pack(rest);
        }
        std::cerr << kUsage;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
