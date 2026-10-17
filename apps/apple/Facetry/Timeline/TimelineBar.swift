// The timeline bar (Fusion-style): one chip per feature, a draggable rollback marker, and the
// details of the selected feature. The web app has the same in apps/web/src/components/Timeline.tsx.

import SwiftUI

struct TimelineBar: View {
    let state: SessionState
    let model: DocumentModel
    /// The feature whose details are open; the browser panel can open one too.
    @Binding var selected: String?
    @State private var dragIndex: Int?

    private let chipWidth = 120.0
    private let spacing = 4.0
    private var pitch: Double { chipWidth + spacing }

    var body: some View {
        let features = state.features
        let marker = dragIndex ?? state.rollback ?? features.count
        // No scroll view: inside one, iPadOS gives the marker's drag to scrolling. Long timelines
        // will need scrolling with a separate drag area for the marker.
        HStack(spacing: spacing) {
            ForEach(Array(features.enumerated()), id: \.element.id) { index, feature in
                chip(feature, index: index, afterMarker: index >= marker)
            }
        }
        .padding(.horizontal, 6)
        .overlay(alignment: .leading) { markerHandle(marker, count: features.count) }
        .padding(.vertical, 6)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
    }

    private func chip(_ feature: SessionState.Feature, index: Int, afterMarker: Bool) -> some View {
        Button {
            selected = selected == feature.id ? nil : feature.id
        } label: {
            HStack(spacing: 4) {
                Image(systemName: icon(feature.type))
                Text(feature.name).lineLimit(1)
                Spacer(minLength: 0)
                if feature.status == "error" || feature.status == "warning" {
                    Image(systemName: "exclamationmark.triangle.fill")
                }
            }
            .font(.caption)
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .frame(width: chipWidth)
            .foregroundStyle(statusColor(feature.status))
            .overlay(
                RoundedRectangle(cornerRadius: 6).stroke(
                    selected == feature.id ? Color.accentColor : Color.secondary.opacity(0.3))
            )
            .opacity(feature.suppressed || afterMarker ? 0.45 : 1)
        }
        .buttonStyle(.plain)
        .popover(
            isPresented: Binding(
                get: { selected == feature.id },
                set: { if !$0, selected == feature.id { selected = nil } })
        ) {
            FeatureDetails(
                feature: feature, index: index, state: state, model: model,
                close: { selected = nil })
        }
    }

    /// The rollback marker: drag it between chips; features after it are not computed.
    private func markerHandle(_ marker: Int, count: Int) -> some View {
        Capsule()
            .fill(Color.accentColor)
            .frame(width: 8)
            .padding(.vertical, -4)
            // A wider touch area; the gesture is attached before the offset so both move with it.
            .padding(.horizontal, 10)
            .contentShape(Rectangle())
            .highPriorityGesture(
                DragGesture(minimumDistance: 2)
                    .onChanged { value in
                        let start = Double(state.rollback ?? count) * pitch
                        let index = Int(((start + value.translation.width) / pitch).rounded())
                        dragIndex = min(max(index, 0), count)
                    }
                    .onEnded { _ in
                        guard let index = dragIndex else { return }
                        dragIndex = nil
                        let rollback = index == count ? nil : index
                        if rollback != state.rollback {
                            Task { await model.edit([.setRollback(index: rollback)]) }
                        }
                    }
            )
            .offset(x: 6 + Double(marker) * pitch - spacing / 2 - 14)
            .accessibilityLabel("Rollback marker")
            .accessibilityValue("after \(marker) of \(count) features")
    }

    private func icon(_ type: String) -> String {
        switch type {
        case "sketch": "pencil"
        case "extrude": "arrow.up.square"
        case "fillet": "circle.lefthalf.filled"
        default: "questionmark.square"
        }
    }
}

/// Status color shared by the chip and the details.
private func statusColor(_ status: String) -> Color {
    switch status {
    case "warning": .orange
    case "error": .red
    default: .primary
    }
}

private func statusLabel(_ status: String) -> String {
    switch status {
    case "ok": "OK"
    case "warning": "Warning"
    case "error": "Error"
    case "suppressed": "Suppressed"
    case "rolledBack": "After the rollback marker"
    default: "Not computed yet"
    }
}

/// Details of one feature: name, values, status, broken references and actions.
private struct FeatureDetails: View {
    let feature: SessionState.Feature
    let index: Int
    let state: SessionState
    let model: DocumentModel
    let close: () -> Void
    @State private var name = ""
    @State private var values: [String: String] = [:]
    @State private var confirmingDelete = false

    /// The numbers a feature type exposes for editing (sketch geometry is edited with M1.3).
    private var fields: [String] {
        switch feature.type {
        case "extrude": ["distance"]
        case "fillet": ["radius"]
        default: []
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            TextField("Name", text: $name)
                .textFieldStyle(.roundedBorder)
                .onSubmit {
                    let trimmed = name.trimmingCharacters(in: .whitespaces)
                    if !trimmed.isEmpty, trimmed != feature.name {
                        Task { await model.edit([.renameFeature(id: feature.id, name: trimmed)]) }
                    }
                }
            Text(statusLabel(feature.status)).foregroundStyle(statusColor(feature.status))
            ForEach(feature.messages, id: \.self) { message in
                Text(message).font(.caption).foregroundStyle(.secondary)
            }
            ForEach(fields, id: \.self) { field in
                HStack {
                    Text(field).frame(width: 64, alignment: .leading)
                    TextField(field, text: binding(for: field))
                        .textFieldStyle(.roundedBorder)
                        .onSubmit { commit(field) }
                }
                .help("A number, or the name of a parameter")
            }
            if feature.type == "sketch" {
                HStack {
                    Text(sketchSummary).font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button("Edit sketch") {
                        close()
                        Task { await model.editSketch(feature.id) }
                    }
                    .disabled(model.sketching != nil)
                }
            }
            if feature.type == "extrude" {
                HStack {
                    Text(profileSummary).font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button("Choose profiles") {
                        model.profiles = feature.id
                        close()
                    }
                }
            }
            // Fillets repair their edges here; sketches report lost references in their messages.
            ForEach(feature.type == "fillet" ? feature.brokenReferences : [], id: \.self) {
                reference in
                brokenReference(reference)
            }
            HStack {
                Button(feature.suppressed ? "Unsuppress" : "Suppress") {
                    Task {
                        await model.edit([
                            .suppressFeature(id: feature.id, suppressed: !feature.suppressed)
                        ])
                    }
                }
                Button("Roll back to here") {
                    Task { await model.edit([.setRollback(index: index + 1)]) }
                }
                Button("Delete", role: .destructive) { confirmingDelete = true }
            }
        }
        .padding()
        .frame(width: 320)
        .onAppear {
            name = feature.name
            for field in fields { values[field] = display(feature.definition[field]) }
        }
        .confirmationDialog(
            "Delete \"\(feature.name)\"?", isPresented: $confirmingDelete, titleVisibility: .visible
        ) {
            Button("Delete", role: .destructive) {
                close()
                Task { await model.edit([.deleteFeature(id: feature.id)]) }
            }
        } message: {
            if !feature.dependents.isEmpty {
                Text("These features use it and will fail: \(dependentNames).")
            }
        }
    }

    /// The profiles a sketch has.
    private var sketchSummary: String {
        let count = feature.regions?.count ?? 0
        return count == 0 ? "No closed profile" : count == 1 ? "1 profile" : "\(count) profiles"
    }

    /// The profiles the extrusion takes.
    private var profileSummary: String {
        guard case .array(let regions)? = feature.definition["regions"], !regions.isEmpty else {
            return "Profile: the sketch's only one"
        }
        return regions.count == 1 ? "1 profile" : "\(regions.count) profiles"
    }

    private var dependentNames: String {
        feature.dependents.map { id in
            state.features.first { $0.id == id }?.name ?? id
        }.joined(separator: ", ")
    }

    private func binding(for field: String) -> Binding<String> {
        Binding(get: { values[field] ?? "" }, set: { values[field] = $0 })
    }

    private func display(_ value: JSONValue?) -> String {
        if let number = value?.number { return number.formatted() }
        return value?.string ?? ""
    }

    /// A number, or else the name of a parameter.
    private func commit(_ field: String) {
        let text = (values[field] ?? "").trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty else { return }
        let value: JSONValue = Double(text).map { .number($0) } ?? .string(text)
        guard value != feature.definition[field] else { return }
        var definition = feature.definition
        definition[field] = value
        Task { await model.edit([.replaceFeature(definition)]) }
    }

    private func brokenReference(_ reference: Int) -> some View {
        let shadow = feature.definition["edges"]?[reference]?["shadow"]
        let place: String
        if let type = shadow?["type"]?.string, case .array(let centroid)? = shadow?["centroid"] {
            let coordinates = centroid.compactMap(\.number).map {
                $0.formatted(.number.precision(.fractionLength(0...2)))
            }
            place = "a \(type) at (\(coordinates.joined(separator: ", ")))"
        } else {
            place = "unknown"
        }
        let active = model.repair == DocumentModel.Repair(feature: feature.id, index: reference)
        return VStack(alignment: .leading, spacing: 4) {
            Text("Edge \(reference + 1) is missing; it was \(place).").foregroundStyle(.red)
            Button(active ? "Cancel" : "Pick replacement") {
                model.repair =
                    active ? nil : DocumentModel.Repair(feature: feature.id, index: reference)
                if !active { close() }
            }
        }
    }
}
