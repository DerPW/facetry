#include "facetry/core/file_format.hpp"

#include "facetry/core/error.hpp"
#include "facetry/core/version.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace facetry::core {
namespace {

using nlohmann::ordered_json;

constexpr const char* kFormat = "facetry";
constexpr const char* kManifest = "manifest.json";
constexpr const char* kDocument = "document.json";
constexpr const char* kThumbnail = "thumbnail.png";
// Refuse entries that unpack to more than this, so a crafted file cannot exhaust memory.
constexpr mz_uint64 kMaxEntrySize = 256ull * 1024 * 1024;

[[noreturn]] void invalid(const std::string& message) {
    throw Error(ErrorCode::InvalidArgument, "Not a valid Facetry file: " + message);
}

class ZipWriter {
  public:
    ZipWriter() {
        mz_zip_zero_struct(&zip_);
        if (mz_zip_writer_init_heap(&zip_, 0, 16 * 1024) == MZ_FALSE) {
            throw Error(ErrorCode::OutOfMemory, "Could not start a zip archive");
        }
    }
    ~ZipWriter() {
        mz_zip_writer_end(&zip_);
    }
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    void add(const char* name, std::span<const std::uint8_t> data, bool compress) {
        const auto level = static_cast<mz_uint>(compress ? MZ_DEFAULT_LEVEL : MZ_NO_COMPRESSION);
        if (mz_zip_writer_add_mem(&zip_, name, data.data(), data.size(), level) == MZ_FALSE) {
            throw Error(ErrorCode::Internal, std::string("Could not add ") + name + " to the file");
        }
    }

    std::vector<std::uint8_t> finish() {
        void* buffer = nullptr;
        size_t size = 0;
        if (mz_zip_writer_finalize_heap_archive(&zip_, &buffer, &size) == MZ_FALSE) {
            throw Error(ErrorCode::Internal, "Could not finish the file");
        }
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        std::vector<std::uint8_t> result(bytes, bytes + size);
        mz_free(buffer);
        return result;
    }

  private:
    mz_zip_archive zip_{};
};

class ZipReader {
  public:
    explicit ZipReader(std::span<const std::uint8_t> bytes) {
        mz_zip_zero_struct(&zip_);
        if (mz_zip_reader_init_mem(&zip_, bytes.data(), bytes.size(), 0) == MZ_FALSE) {
            invalid(std::string("the zip container is damaged (") +
                    mz_zip_get_error_string(mz_zip_get_last_error(&zip_)) + ")");
        }
    }
    ~ZipReader() {
        mz_zip_reader_end(&zip_);
    }
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    [[nodiscard]] bool has(const char* name) {
        return mz_zip_reader_locate_file(&zip_, name, nullptr, 0) >= 0;
    }

    [[nodiscard]] std::vector<std::uint8_t> read(const char* name) {
        const int index = mz_zip_reader_locate_file(&zip_, name, nullptr, 0);
        if (index < 0) {
            invalid(std::string("it has no ") + name);
        }
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&zip_, static_cast<mz_uint>(index), &stat) == MZ_FALSE ||
            stat.m_uncomp_size > kMaxEntrySize) {
            invalid(std::string(name) + " is damaged or too large");
        }
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip_, static_cast<mz_uint>(index), &size, 0);
        if (data == nullptr) {
            invalid(std::string(name) + " could not be unpacked");
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::vector<std::uint8_t> result(bytes, bytes + size);
        mz_free(data);
        return result;
    }

  private:
    mz_zip_archive zip_{};
};

std::string_view asText(std::span<const std::uint8_t> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::span<const std::uint8_t> asBytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

bool isZip(std::span<const std::uint8_t> bytes) {
    static constexpr std::uint8_t kMagic[] = {'P', 'K', 3, 4};
    return bytes.size() >= 4 && std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin());
}

FileContents readContainer(std::span<const std::uint8_t> bytes) {
    ZipReader zip(bytes);
    const std::vector<std::uint8_t> manifestBytes = zip.read(kManifest);
    const auto manifest = ordered_json::parse(asText(manifestBytes), nullptr, false);
    if (!manifest.is_object() || manifest.value("format", "") != kFormat ||
        !manifest.contains("formatVersion") || !manifest["formatVersion"].is_number_unsigned()) {
        invalid("manifest.json is not a Facetry manifest");
    }
    const auto version = manifest["formatVersion"].get<std::uint64_t>();
    if (version > static_cast<std::uint64_t>(kFileFormatVersion)) {
        throw Error(ErrorCode::Unsupported,
                    "This file was saved by a newer version of Facetry (file format " +
                        std::to_string(version) + ")");
    }
    FileContents contents;
    contents.document = parseDocument(asText(zip.read(kDocument)));
    if (zip.has(kThumbnail)) {
        contents.thumbnail = zip.read(kThumbnail);
    }
    return contents;
}

}  // namespace

std::vector<std::uint8_t> writeFile(const Document& document,
                                    std::span<const std::uint8_t> thumbnail) {
    const std::string manifest = ordered_json{{"format", kFormat},
                                              {"formatVersion", kFileFormatVersion},
                                              {"createdWith", std::string(version())}}
                                     .dump(2) +
                                 "\n";
    const std::string json = toJson(document);
    ZipWriter zip;
    zip.add(kManifest, asBytes(manifest), false);
    zip.add(kDocument, asBytes(json), true);
    if (!thumbnail.empty()) {
        zip.add(kThumbnail, thumbnail, false);
    }
    return zip.finish();
}

FileContents readFile(std::span<const std::uint8_t> bytes) {
    if (isZip(bytes)) {
        return readContainer(bytes);
    }
    // Not a zip: a bare document.json, or a provisional file of the M0.5 Apple app.
    const auto json = ordered_json::parse(asText(bytes), nullptr, false);
    if (json.is_object() && json.value("format", "") == "facetry-document") {
        return {.document = parseDocument(asText(bytes)), .thumbnail = {}};
    }
    if (json.is_object() && json.value("format", "") == kFormat &&
        json.value("formatVersion", -1) == 0) {
        return {};  // M0.5 files hold no model
    }
    invalid("it is neither a .facetry file nor a Facetry document");
}

}  // namespace facetry::core
