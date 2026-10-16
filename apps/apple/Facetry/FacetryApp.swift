import SwiftUI

@main
struct FacetryApp: App {
    var body: some Scene {
        DocumentGroup(newDocument: FacetryDocument()) { file in
            ContentView(document: file.$document)
        }
    }
}
