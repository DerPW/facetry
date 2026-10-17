import Foundation
import Testing

@testable import Facetry

struct DocumentTests {
    @Test func newDocumentsStartFromTheExample() throws {
        let document = FacetryDocument()
        #expect(document.data.starts(with: Array("PK".utf8)))
        try FacetryDocument.validate(document.data)
    }

    @Test func savedFilesOpenAgain() throws {
        let core = try Core()
        let session = try core.openSession(fileData: FacetryDocument.example)
        try core.apply(session, [.setParameter(name: "width", value: 12)])
        let saved = try core.saveFile(session)
        let reopened = try core.openSession(fileData: saved)
        #expect(try core.state(of: reopened).parameters.first?.value == 12)
    }

    @Test func provisionalFilesOpenAsEmptyDocuments() throws {
        let provisional = Data(
            #"{"createdWith": "0.0.0", "format": "facetry", "formatVersion": 0}"#.utf8)
        try FacetryDocument.validate(provisional)
        let core = try Core()
        #expect(try core.state(of: core.openSession(fileData: provisional)).features.isEmpty)
    }

    @Test func newerChangesStopARunningRegeneration() async throws {
        let engine = try DocumentEngine(fileData: FacetryDocument.example)
        let obsolete = engine.control.next()
        let current = engine.control.next()  // a newer change arrived
        #expect(try await engine.regenerate(generation: obsolete) { _ in } == nil)
        #expect(try await engine.needsRegeneration())
        let snapshot = try await engine.regenerate(generation: current) { _ in }
        #expect(snapshot?.state.features.map(\.status) == ["ok", "ok", "ok"])
        // The obsolete run stopped inside the sketch, which reports progress while it finds its
        // profiles, so all three features run now.
        #expect(snapshot?.state.regeneration.executed == 3)
        #expect(try await engine.needsRegeneration() == false)
    }

    @Test func rejectsOtherAndNewerFiles() {
        #expect(throws: (any Error).self) { try FacetryDocument.validate(Data("not json".utf8)) }
        let newer = Data(#"{"format": "facetry", "formatVersion": 99}"#.utf8)
        #expect(throws: (any Error).self) { try FacetryDocument.validate(newer) }
    }
}
