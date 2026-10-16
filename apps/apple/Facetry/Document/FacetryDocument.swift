// The .facetry document (docs/adr/0008-native-file-format.md). The file's bytes are read and
// written by the core; this type only carries them between SwiftUI and the model.

import SwiftUI
import UniformTypeIdentifiers

extension UTType {
    /// Declared as an exported type in the app's Info.plist (apps/apple/project.yml).
    static let facetryDocument = UTType(exportedAs: "io.github.derpw.facetry.document")
}

struct FacetryDocument: FileDocument {
    static let readableContentTypes: [UTType] = [.facetryDocument]

    /// The file as the core writes it.
    var data: Data

    /// A new document. Until there are modeling tools (M1.3–M1.5) it starts as the example model.
    init() {
        data = Self.example
    }

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw CocoaError(.fileReadCorruptFile)
        }
        try Self.validate(data)
        self.data = data
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }

    /// Throws if the core cannot open `data`, so the system reports a broken or newer file
    /// instead of opening an empty window.
    static func validate(_ data: Data) throws {
        let core = try Core()
        try core.release(try core.openSession(fileData: data))
    }

    /// The bundled example (tests/documents/corner-fillet.facetry).
    static var example: Data {
        guard let url = Bundle.main.url(forResource: "corner-fillet", withExtension: "facetry"),
            let data = try? Data(contentsOf: url)
        else { return Data() }
        return data
    }
}
