// The native file format (docs/adr/0008-native-file-format.md).
#include "facetry/core/file_format.hpp"

#include "facetry/core/error.hpp"

#include "corner_fillet.hpp"
#include "facetry_core.h"

#include <doctest.h>
#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace facetry::core;
using namespace facetry::test;

namespace {

std::vector<std::uint8_t> readBytes(const std::string& name) {
    std::ifstream in(std::filesystem::path(FACETRY_DOCUMENTS_DIR) / name, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    const std::string data = text.str();
    return {data.begin(), data.end()};
}

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return {text.begin(), text.end()};
}

// A zip with the given entries, for files this code would never write itself.
std::vector<std::uint8_t> zip(const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive archive{};
    REQUIRE(mz_zip_writer_init_heap(&archive, 0, 1024));
    for (const auto& [name, content] : entries) {
        REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), 0));
    }
    void* buffer = nullptr;
    size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size));
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);
    std::vector<std::uint8_t> result(bytes, bytes + size);
    mz_free(buffer);
    mz_zip_writer_end(&archive);
    return result;
}

ErrorCode readError(const std::vector<std::uint8_t>& bytes) {
    try {
        static_cast<void>(readFile(bytes));
    } catch (const Error& error) {
        return error.code();
    }
    FAIL("expected an error");
    return ErrorCode::Internal;
}

const std::string kManifest = R"({"format": "facetry", "formatVersion": 1})";

}  // namespace

TEST_CASE("files round-trip, with and without a thumbnail") {
    const Document document = cornerFilletSession().document();
    const std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 1, 2, 3};
    for (const auto& thumbnail : {std::vector<std::uint8_t>{}, png}) {
        const auto bytes = writeFile(document, thumbnail);
        CHECK(bytes.size() > 4);
        CHECK(bytes[0] == 'P');
        const FileContents contents = readFile(bytes);
        CHECK(contents.document == document);
        CHECK(contents.thumbnail == thumbnail);
        CHECK(writeFile(document, thumbnail) == bytes);  // the same input gives the same bytes
    }
}

TEST_CASE("the checked-in example files still open") {
    for (const std::string name :
         {"corner-fillet", "corner-fillet-inch", "sketch-planes", "plate-with-hole"}) {
        CAPTURE(name);
        const FileContents contents = readFile(readBytes(name + ".facetry"));
        const auto json = readBytes(name + ".json");
        CHECK(contents.document == parseDocument(std::string(json.begin(), json.end())));
        CHECK(contents.thumbnail.empty());
    }
}

TEST_CASE("older and simpler forms open too") {
    // A bare document.json, as `facetry-cli unpack` writes it.
    const auto json = readBytes("corner-fillet.json");
    CHECK(readFile(json).document.root.timeline.size() == 3);
    // A provisional file of the M0.5 Apple app: no model, opens as an empty document.
    const FileContents provisional =
        readFile(bytesOf(R"({"createdWith": "0.0.0", "format": "facetry", "formatVersion": 0})"));
    CHECK(provisional.document == Document{});
}

TEST_CASE("damaged, foreign and newer files are rejected") {
    CHECK(readError(bytesOf("hello")) == ErrorCode::InvalidArgument);
    CHECK(readError(bytesOf(R"({"format": "facetry", "formatVersion": 7})")) ==
          ErrorCode::InvalidArgument);
    CHECK(readError({'P', 'K', 3, 4, 0, 0, 0}) == ErrorCode::InvalidArgument);
    CHECK(readError(zip({{"document.json", "{}"}})) == ErrorCode::InvalidArgument);
    CHECK(readError(zip({{"manifest.json", kManifest}})) == ErrorCode::InvalidArgument);
    CHECK(readError(zip({{"manifest.json", R"({"format": "other", "formatVersion": 1})"},
                         {"document.json", "{}"}})) == ErrorCode::InvalidArgument);
    const auto json = readBytes("corner-fillet.json");
    CHECK(readError(zip({{"manifest.json", R"({"format": "facetry", "formatVersion": 2})"},
                         {"document.json", std::string(json.begin(), json.end())}})) ==
          ErrorCode::Unsupported);
    // Unknown entries are ignored.
    CHECK_NOTHROW(
        static_cast<void>(readFile(zip({{"manifest.json", kManifest},
                                        {"document.json", std::string(json.begin(), json.end())},
                                        {"attachments/readme.txt", "later"}}))));
}

TEST_CASE("sessions open and save files through the C API") {
    facetry_context* ctx = nullptr;
    REQUIRE(facetry_context_create(&ctx) == FACETRY_OK);
    const auto file = readBytes("corner-fillet.facetry");
    facetry_handle session = 0;
    REQUIRE(facetry_session_open_file(ctx, file.data(), file.size(), &session) == FACETRY_OK);

    const uint8_t* data = nullptr;
    size_t size = 0;
    REQUIRE(facetry_session_save_file(ctx, session, nullptr, 0, &data, &size) == FACETRY_OK);
    const std::vector<std::uint8_t> saved(data, data + size);
    CHECK(readFile(saved).document == readFile(file).document);

    const uint8_t png[] = {0x89, 'P', 'N', 'G'};
    REQUIRE(facetry_session_save_file(ctx, session, png, sizeof png, &data, &size) == FACETRY_OK);
    CHECK(readFile(std::vector<std::uint8_t>(data, data + size)).thumbnail.size() == 4);

    facetry_handle other = 0;
    const std::vector<std::uint8_t> junk = bytesOf("junk");
    CHECK(facetry_session_open_file(ctx, junk.data(), junk.size(), &other) ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(facetry_session_open_file(ctx, nullptr, 3, &other) == FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(facetry_session_save_file(ctx, session, nullptr, 0, nullptr, &size) ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    facetry_context_destroy(ctx);
}
