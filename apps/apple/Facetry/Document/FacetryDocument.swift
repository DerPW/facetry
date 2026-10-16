// The .facetry document. For now a document is a small JSON file with a format marker and
// version 0. The native file format (docs/adr/0008-native-file-format.md), a zip container
// written by the core, and the model itself arrive with M1.1.

import SwiftUI
import UniformTypeIdentifiers

extension UTType {
    /// Declared as an exported type in the app's Info.plist (apps/apple/project.yml).
    static let facetryDocument = UTType(exportedAs: "io.github.derpw.facetry.document")
}

struct FacetryDocument: FileDocument {
    static let readableContentTypes: [UTType] = [.facetryDocument]

    /// Provisional on-disk content (format version 0 = before ADR-0008).
    struct Content: Codable, Equatable {
        var format = "facetry"
        var formatVersion = 0
        var createdWith =
            Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString")
            as? String ?? "?"
    }

    var content = Content()

    init() {}

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw CocoaError(.fileReadCorruptFile)
        }
        content = try Self.decode(data)
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: try Self.encode(content))
    }

    static func decode(_ data: Data) throws -> Content {
        let content = try JSONDecoder().decode(Content.self, from: data)
        guard content.format == "facetry" else { throw CocoaError(.fileReadCorruptFile) }
        guard content.formatVersion <= Content().formatVersion else {
            throw CocoaError(.fileReadUnsupportedScheme)
        }
        return content
    }

    static func encode(_ content: Content) throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(content)
    }
}
