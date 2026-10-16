#pragma once

// The native file format (docs/adr/0008-native-file-format.md): a .facetry file is a zip
// container with manifest.json, document.json and an optional thumbnail.png.

#include "facetry/core/document.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace facetry::core {

// Version of the container layout, written to manifest.json. The document inside has its own
// version (kDocumentFormatVersion).
inline constexpr int kFileFormatVersion = 1;

struct FileContents {
    Document document;
    std::vector<std::uint8_t> thumbnail;  // PNG; empty if the file has none
};

// Writes a .facetry file. The output only depends on its input: entries have a fixed time, so the
// same document always gives the same bytes. `thumbnail` (a PNG) may be empty.
[[nodiscard]] std::vector<std::uint8_t> writeFile(const Document& document,
                                                  std::span<const std::uint8_t> thumbnail = {});

// Reads a .facetry file. Also accepts, as older or simpler forms:
//   - a bare document.json (what `facetry-cli unpack` writes),
//   - the provisional JSON files of the M0.5 Apple app (format version 0), which contain no model
//     and open as an empty document.
// Throws Error(InvalidArgument) for anything else and Error(Unsupported) for newer versions.
[[nodiscard]] FileContents readFile(std::span<const std::uint8_t> bytes);

}  // namespace facetry::core
