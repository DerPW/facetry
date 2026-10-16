import Foundation
import Testing

@testable import Facetry

struct DocumentTests {
    @Test func roundTripsThroughJSON() throws {
        let content = FacetryDocument.Content()
        let data = try FacetryDocument.encode(content)
        #expect(try FacetryDocument.decode(data) == content)
        let text = try #require(String(data: data, encoding: .utf8))
        #expect(text.contains("\"format\" : \"facetry\""))
    }

    @Test func rejectsOtherFiles() {
        let other = Data(
            #"{"format": "something-else", "formatVersion": 0, "createdWith": "1"}"#.utf8)
        #expect(throws: (any Error).self) { try FacetryDocument.decode(other) }
        #expect(throws: (any Error).self) { try FacetryDocument.decode(Data("not json".utf8)) }
    }

    @Test func rejectsNewerFormatVersions() {
        let newer = Data(#"{"format": "facetry", "formatVersion": 99, "createdWith": "9.0"}"#.utf8)
        #expect(throws: (any Error).self) { try FacetryDocument.decode(newer) }
    }
}
