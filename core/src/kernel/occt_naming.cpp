// Persistent naming in the kernel adapter (docs/adr/0006-persistent-naming.md). Every naming
// operation goes through FaceNames and nameTopology(), so all features name the same way:
//   1. faces get names from the operation's history (kept, generated from a named input, or a
//      fixed role),
//   2. edges and vertices are named by the faces around them,
//   3. equal names are told apart by geometric order, never by OpenCASCADE's output order.
#include "facetry/core/naming.hpp"

#include "occt_common.hpp"

#include <BOPAlgo_BuilderFace.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace facetry::core {
namespace {

using AncestorMap = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>,
                                               TopTools_ShapeMapHasher>;

Vec3 toVec3(const gp_Pnt& point) {
    return {point.X(), point.Y(), point.Z()};
}

const char* surfaceType(GeomAbs_SurfaceType type) {
    switch (type) {
        case GeomAbs_Plane:
            return "plane";
        case GeomAbs_Cylinder:
            return "cylinder";
        case GeomAbs_Cone:
            return "cone";
        case GeomAbs_Sphere:
            return "sphere";
        case GeomAbs_Torus:
            return "torus";
        case GeomAbs_BezierSurface:
        case GeomAbs_BSplineSurface:
            return "spline";
        case GeomAbs_SurfaceOfRevolution:
            return "revolution";
        case GeomAbs_SurfaceOfExtrusion:
            return "extrusion";
        case GeomAbs_OffsetSurface:
            return "offset";
        case GeomAbs_OtherSurface:
            break;
    }
    return "other";
}

const char* curveType(GeomAbs_CurveType type) {
    switch (type) {
        case GeomAbs_Line:
            return "line";
        case GeomAbs_Circle:
            return "circle";
        case GeomAbs_Ellipse:
            return "ellipse";
        case GeomAbs_Hyperbola:
            return "hyperbola";
        case GeomAbs_Parabola:
            return "parabola";
        case GeomAbs_BezierCurve:
        case GeomAbs_BSplineCurve:
            return "spline";
        case GeomAbs_OffsetCurve:
            return "offset";
        case GeomAbs_OtherCurve:
            break;
    }
    return "other";
}

ElementGeometry geometryOf(const TopoDS_Shape& element) {
    switch (element.ShapeType()) {
        case TopAbs_FACE: {
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(element, properties);
            const BRepAdaptor_Surface surface(TopoDS::Face(element));
            return {surfaceType(surface.GetType()), toVec3(properties.CentreOfMass()),
                    properties.Mass()};
        }
        case TopAbs_EDGE: {
            const TopoDS_Edge& edge = TopoDS::Edge(element);
            if (BRep_Tool::Degenerated(edge)) {
                TopoDS_Vertex first;
                TopoDS_Vertex last;
                TopExp::Vertices(edge, first, last);
                return {"degenerate", toVec3(BRep_Tool::Pnt(first)), 0.0};
            }
            GProp_GProps properties;
            BRepGProp::LinearProperties(edge, properties);
            const BRepAdaptor_Curve curve(edge);
            return {curveType(curve.GetType()), toVec3(properties.CentreOfMass()),
                    properties.Mass()};
        }
        case TopAbs_VERTEX:
            return {"point", toVec3(BRep_Tool::Pnt(TopoDS::Vertex(element))), 0.0};
        default:
            break;
    }
    throw Error(ErrorCode::Internal, "Only faces, edges and vertices have a fingerprint");
}

// Sort key for telling equally named elements apart: the centroid rounded to a grid well below
// the modeling tolerance, so that the order is the same on every platform.
std::tuple<long long, long long, long long, long long> geometricOrder(const TopoDS_Shape& element) {
    constexpr double grid = 1e-6;
    const ElementGeometry geometry = geometryOf(element);
    return {std::llround(geometry.centroid.x / grid), std::llround(geometry.centroid.y / grid),
            std::llround(geometry.centroid.z / grid), std::llround(geometry.size / grid)};
}

// Appends "#1", "#2", … to names that occur more than once, in geometric order.
void disambiguate(const kernel::ShapeMap& elements, std::vector<std::string>& names) {
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < names.size(); ++i) {
        groups[names[i]].push_back(i);
    }
    for (auto& [name, indices] : groups) {
        if (indices.size() < 2) {
            continue;
        }
        std::vector<std::pair<std::tuple<long long, long long, long long, long long>, std::size_t>>
            order;
        for (const std::size_t index : indices) {
            order.emplace_back(geometricOrder(elements(static_cast<int>(index) + 1)), index);
        }
        std::stable_sort(order.begin(), order.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t k = 0; k < order.size(); ++k) {
            names[order[k].second] = name + "#" + std::to_string(k + 1);
        }
    }
}

// Names elements of one kind after the sorted, distinct names of the faces around them, e.g.
// "edge{f2:end|f2:side[f1/l2]}".
std::vector<std::string> nameByFaces(const Shape::Impl& result, TopAbs_ShapeEnum kind,
                                     const kernel::ShapeMap& elements, const char* prefix,
                                     const std::vector<std::string>& faceNames) {
    AncestorMap ancestors;
    TopExp::MapShapesAndUniqueAncestors(result.shape, kind, TopAbs_FACE, ancestors);
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(elements.Extent()));
    for (int i = 1; i <= elements.Extent(); ++i) {
        std::set<std::string> around;
        if (const NCollection_List<TopoDS_Shape>* faces = ancestors.Seek(elements(i))) {
            for (const TopoDS_Shape& face : *faces) {
                const int id = result.faces.FindIndex(face);
                if (id > 0) {
                    around.insert(faceNames[static_cast<std::size_t>(id) - 1]);
                }
            }
        }
        std::string name = prefix;
        name += '{';
        for (auto it = around.begin(); it != around.end(); ++it) {
            if (it != around.begin()) {
                name += '|';
            }
            name += *it;
        }
        name += '}';
        names.push_back(std::move(name));
    }
    disambiguate(elements, names);
    return names;
}

// Collects face names for an operation's result from its history.
class FaceNames {
  public:
    explicit FaceNames(const Shape::Impl& result)
        : result_(result), candidates_(static_cast<std::size_t>(result.faces.Extent())) {}

    // Names `face` if it is part of the result.
    void add(const TopoDS_Shape& face, const std::string& name) {
        const int id = result_.faces.FindIndex(face);
        if (id > 0) {
            candidates_[static_cast<std::size_t>(id) - 1].insert(name);
        }
    }

    // Faces of the input body keep their names, whether unchanged or modified (trimmed, split).
    void inherit(BRepBuilderAPI_MakeShape& maker, const NamedShape& input) {
        const Shape::Impl& data = input.shape.impl();
        for (int i = 1; i <= data.faces.Extent(); ++i) {
            const std::string& name = input.faces[static_cast<std::size_t>(i) - 1];
            add(data.faces(i), name);
            for (const TopoDS_Shape& modified : maker.Modified(data.faces(i))) {
                add(modified, name);
            }
        }
    }

    // Faces generated from `source` (an input edge or vertex) are named `name`.
    void generated(BRepBuilderAPI_MakeShape& maker, const TopoDS_Shape& source,
                   const std::string& name) {
        for (const TopoDS_Shape& shape : maker.Generated(source)) {
            if (shape.ShapeType() == TopAbs_FACE) {
                add(shape, name);
            }
        }
    }

    // Faces without history are "<feature>:new". A face with several names, for example two
    // faces merged into one, takes the smallest so the choice does not depend on the order in
    // which the operation reported them.
    [[nodiscard]] std::vector<std::string> finish(std::string_view feature) const {
        std::vector<std::string> names;
        names.reserve(candidates_.size());
        for (const std::set<std::string>& candidates : candidates_) {
            names.push_back(candidates.empty() ? std::string(feature) + ":new"
                                               : *candidates.begin());
        }
        return names;
    }

  private:
    const Shape::Impl& result_;
    std::vector<std::set<std::string>> candidates_;
};

NamedShape nameTopology(Shape result, std::vector<std::string> faceNames) {
    const Shape::Impl& data = result.impl();
    disambiguate(data.faces, faceNames);
    NamedShape named;
    named.edges = nameByFaces(data, TopAbs_EDGE, data.edges, "edge", faceNames);
    named.vertices = nameByFaces(data, TopAbs_VERTEX, data.vertices, "vertex", faceNames);
    named.faces = std::move(faceNames);
    named.shape = std::move(result);
    return named;
}

constexpr double kPointTolerance = 1e-7;

bool samePoint(double x0, double y0, double x1, double y1) {
    return std::hypot(x1 - x0, y1 - y0) <= kPointTolerance;
}

void checkLoop(std::span<const ProfileSegment> loop) {
    using Kind = ProfileSegment::Kind;
    if (loop.size() == 1 && loop[0].kind == Kind::Circle) {
        for (const double value : {loop[0].cx, loop[0].cy, loop[0].radius}) {
            kernel::requireFinite(value, "Profile coordinate");
        }
        kernel::requirePositive(loop[0].radius, "Circle radius");
        return;
    }
    const bool curved = std::any_of(loop.begin(), loop.end(), [](const ProfileSegment& segment) {
        return segment.kind == Kind::Arc;
    });
    if (loop.size() < (curved ? 2U : 3U)) {
        throw Error(ErrorCode::InvalidArgument, "A profile needs at least three edges");
    }
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const ProfileSegment& segment = loop[i];
        const ProfileSegment& next = loop[(i + 1) % loop.size()];
        if (segment.kind == Kind::Circle) {
            throw Error(ErrorCode::InvalidArgument,
                        "Circle " + segment.name + " is a profile on its own");
        }
        for (const double value :
             {segment.x0, segment.y0, segment.x1, segment.y1, segment.cx, segment.cy}) {
            kernel::requireFinite(value, "Profile coordinate");
        }
        if (segment.kind == Kind::Line &&
            samePoint(segment.x0, segment.y0, segment.x1, segment.y1)) {
            throw Error(ErrorCode::InvalidArgument,
                        "Profile edge " + segment.name + " has zero length");
        }
        if (segment.kind == Kind::Arc) {
            const double r0 = std::hypot(segment.x0 - segment.cx, segment.y0 - segment.cy);
            const double r1 = std::hypot(segment.x1 - segment.cx, segment.y1 - segment.cy);
            if (r0 < kPointTolerance || samePoint(segment.x0, segment.y0, segment.x1, segment.y1)) {
                throw Error(ErrorCode::InvalidArgument, "Arc " + segment.name + " is degenerate");
            }
            if (std::abs(r1 - r0) > kPointTolerance * std::max(1.0, r0)) {
                throw Error(ErrorCode::InvalidArgument,
                            "Arc " + segment.name + " ends off its circle");
            }
        }
        if (!samePoint(segment.x1, segment.y1, next.x0, next.y0)) {
            throw Error(ErrorCode::InvalidArgument, "Profile edge " + segment.name +
                                                        " does not end where " + next.name +
                                                        " starts; the profile must be closed");
        }
    }
}

}  // namespace

ElementGeometry elementGeometry(const Shape& shape, ElementKind kind, std::uint32_t id) {
    const Shape::Impl& data = shape.impl();
    const kernel::ShapeMap& elements = kind == ElementKind::Face   ? data.faces
                                       : kind == ElementKind::Edge ? data.edges
                                                                   : data.vertices;
    if (id == 0 || id > static_cast<std::uint32_t>(elements.Extent())) {
        throw Error(ErrorCode::InvalidArgument,
                    std::string(toString(kind)) + " ID " + std::to_string(id) + " does not exist");
    }
    return kernel::guard("Measuring an element",
                         [&] { return geometryOf(elements(static_cast<int>(id))); });
}

Vec3 SketchFrame::normal() const noexcept {
    return {xAxis.y * yAxis.z - xAxis.z * yAxis.y, xAxis.z * yAxis.x - xAxis.x * yAxis.z,
            xAxis.x * yAxis.y - xAxis.y * yAxis.x};
}

Vec3 SketchFrame::at(double x, double y) const noexcept {
    return {origin.x + xAxis.x * x + yAxis.x * y, origin.y + xAxis.y * x + yAxis.y * y,
            origin.z + xAxis.z * x + yAxis.z * y};
}

SketchFrame originPlaneFrame(std::string_view plane) {
    if (plane == "xy") {
        return {};
    }
    if (plane == "xz") {
        return {.origin = {}, .xAxis = {1.0, 0.0, 0.0}, .yAxis = {0.0, 0.0, 1.0}};
    }
    if (plane == "yz") {
        return {.origin = {}, .xAxis = {0.0, 1.0, 0.0}, .yAxis = {0.0, 0.0, 1.0}};
    }
    throw Error(ErrorCode::InvalidArgument,
                "\"" + std::string(plane) + "\" is not an origin plane (xy, xz or yz)");
}

namespace {

gp_Pnt point(const Vec3& p) {
    return {p.x, p.y, p.z};
}

gp_Dir direction(const Vec3& d) {
    return {d.x, d.y, d.z};
}

// The point halfway along an arc, in sketch coordinates.
std::pair<double, double> arcMiddle(const ProfileSegment& arc) {
    const double start = std::atan2(arc.y0 - arc.cy, arc.x0 - arc.cx);
    const double end = std::atan2(arc.y1 - arc.cy, arc.x1 - arc.cx);
    double sweep = arc.clockwise ? start - end : end - start;
    while (sweep <= 0.0) {
        sweep += 2.0 * M_PI;
    }
    const double middle = start + (arc.clockwise ? -sweep : sweep) / 2.0;
    const double radius = std::hypot(arc.x0 - arc.cx, arc.y0 - arc.cy);
    return {arc.cx + radius * std::cos(middle), arc.cy + radius * std::sin(middle)};
}

}  // namespace

NamedShape extrudeProfile(std::span<const ProfileSegment> loop, double distance,
                          std::string_view feature, const SketchFrame& frame) {
    checkLoop(loop);
    kernel::requireFinite(distance, "Extrude distance");
    if (std::abs(distance) < kPointTolerance) {
        throw Error(ErrorCode::InvalidArgument, "Extrude distance must not be zero");
    }
    return kernel::guard("Extruding a profile", [&] {
        const Vec3 n = frame.normal();
        std::vector<TopoDS_Edge> edges;
        BRepBuilderAPI_MakeWire wire;
        if (loop.size() == 1 && loop[0].kind == ProfileSegment::Kind::Circle) {
            const ProfileSegment& circle = loop[0];
            const gp_Ax2 axis(point(frame.at(circle.cx, circle.cy)), direction(n),
                              direction(frame.xAxis));
            edges.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(axis, circle.radius)));
            wire.Add(edges.back());
        } else {
            // One vertex per corner, shared by the two edges meeting there, so the wire is
            // closed without copying edges and every edge of the face is one of ours.
            std::vector<TopoDS_Vertex> corners;
            for (const ProfileSegment& segment : loop) {
                corners.push_back(
                    BRepBuilderAPI_MakeVertex(point(frame.at(segment.x0, segment.y0))));
            }
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const ProfileSegment& segment = loop[i];
                const TopoDS_Vertex& from = corners[i];
                const TopoDS_Vertex& to = corners[(i + 1) % loop.size()];
                if (segment.kind == ProfileSegment::Kind::Arc) {
                    // Through its middle, so the arc runs the right way round.
                    const auto [mx, my] = arcMiddle(segment);
                    const GC_MakeArcOfCircle arc(BRep_Tool::Pnt(from), point(frame.at(mx, my)),
                                                 BRep_Tool::Pnt(to));
                    if (!arc.IsDone()) {
                        throw Error(ErrorCode::Kernel, "Arc " + segment.name + " is degenerate");
                    }
                    edges.push_back(BRepBuilderAPI_MakeEdge(arc.Value(), from, to));
                } else {
                    edges.push_back(BRepBuilderAPI_MakeEdge(from, to));
                }
                wire.Add(edges.back());
            }
        }
        if (!wire.IsDone()) {
            throw Error(ErrorCode::Kernel, "The profile edges do not form a wire");
        }
        // Inside = true orients the face to the area enclosed by the wire, whichever direction
        // the profile runs.
        const gp_Pln plane(gp_Ax3(point(frame.origin), direction(n), direction(frame.xAxis)));
        BRepBuilderAPI_MakeFace face(plane, wire.Wire(), /*Inside=*/true);
        if (!face.IsDone()) {
            throw Error(ErrorCode::Kernel, "The profile does not bound a face");
        }
        BRepPrimAPI_MakePrism prism(face.Face(),
                                    gp_Vec(n.x * distance, n.y * distance, n.z * distance));
        prism.Build();
        if (!prism.IsDone()) {
            throw Error(ErrorCode::Kernel, "Extruding the profile failed");
        }
        Shape result = kernel::checkedResult(prism.Shape(), "Extruding a profile");

        FaceNames names(result.impl());
        const std::string prefix(feature);
        for (std::size_t i = 0; i < loop.size(); ++i) {
            names.generated(prism, edges[i], prefix + ":side[" + loop[i].name + "]");
        }
        names.add(prism.FirstShape(), prefix + ":start");
        names.add(prism.LastShape(), prefix + ":end");
        return nameTopology(std::move(result), names.finish(feature));
    });
}

namespace {

using Kind = ProfileSegment::Kind;

// Where a point in space lies on the sketch plane.
std::pair<double, double> inSketch(const SketchFrame& frame, const gp_Pnt& p) {
    const double dx = p.X() - frame.origin.x;
    const double dy = p.Y() - frame.origin.y;
    const double dz = p.Z() - frame.origin.z;
    return {dx * frame.xAxis.x + dy * frame.xAxis.y + dz * frame.xAxis.z,
            dx * frame.yAxis.x + dy * frame.yAxis.y + dz * frame.yAxis.z};
}

double radiusOf(const ProfileSegment& curve) {
    return curve.kind == Kind::Circle ? curve.radius
                                      : std::hypot(curve.x0 - curve.cx, curve.y0 - curve.cy);
}

// Checks a curve for sketchRegions(). Returns false for degenerate curves, which bound nothing.
bool usable(const ProfileSegment& curve) {
    for (const double value :
         {curve.x0, curve.y0, curve.x1, curve.y1, curve.cx, curve.cy, curve.radius}) {
        kernel::requireFinite(value, "Sketch coordinate");
    }
    switch (curve.kind) {
        case Kind::Line:
            return !samePoint(curve.x0, curve.y0, curve.x1, curve.y1);
        case Kind::Circle:
            if (curve.radius < 0.0) {
                kernel::requirePositive(curve.radius, "Circle radius");
            }
            return curve.radius > kPointTolerance;
        case Kind::Arc: {
            const double r0 = radiusOf(curve);
            const double r1 = std::hypot(curve.x1 - curve.cx, curve.y1 - curve.cy);
            if (r0 <= kPointTolerance || samePoint(curve.x0, curve.y0, curve.x1, curve.y1)) {
                return false;
            }
            if (std::abs(r1 - r0) > kPointTolerance * std::max(1.0, r0)) {
                throw Error(ErrorCode::InvalidArgument,
                            "Arc " + curve.name + " ends off its circle");
            }
            return true;
        }
    }
    return false;
}

// The edge of a curve on the sketch plane: lines and arcs from start to end, circles
// counter-clockwise.
TopoDS_Edge curveEdge(const ProfileSegment& curve, const SketchFrame& frame) {
    const gp_Pnt start = point(frame.at(curve.x0, curve.y0));
    const gp_Pnt end = point(frame.at(curve.x1, curve.y1));
    switch (curve.kind) {
        case Kind::Line:
            return BRepBuilderAPI_MakeEdge(start, end);
        case Kind::Arc: {
            const auto [mx, my] = arcMiddle(curve);
            const GC_MakeArcOfCircle arc(start, point(frame.at(mx, my)), end);
            if (!arc.IsDone()) {
                throw Error(ErrorCode::Kernel, "Arc " + curve.name + " is degenerate");
            }
            return BRepBuilderAPI_MakeEdge(arc.Value());
        }
        case Kind::Circle:
            break;
    }
    const gp_Ax2 axis(point(frame.at(curve.cx, curve.cy)), direction(frame.normal()),
                      direction(frame.xAxis));
    return BRepBuilderAPI_MakeEdge(gp_Circ(axis, curve.radius));
}

// The angle from `from` counter-clockwise to `to`, in [0, 2π).
double sweepBetween(double from, double to) {
    double sweep = std::fmod(to - from, 2.0 * M_PI);
    return sweep < 0.0 ? sweep + 2.0 * M_PI : sweep;
}

// Distance on the sketch plane from (u, v) to a curve.
double distanceTo(const ProfileSegment& curve, double u, double v) {
    if (curve.kind == Kind::Line) {
        const double dx = curve.x1 - curve.x0;
        const double dy = curve.y1 - curve.y0;
        const double t =
            std::clamp(((u - curve.x0) * dx + (v - curve.y0) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
        return std::hypot(u - curve.x0 - t * dx, v - curve.y0 - t * dy);
    }
    const double onCircle = std::abs(std::hypot(u - curve.cx, v - curve.cy) - radiusOf(curve));
    if (curve.kind == Kind::Circle) {
        return onCircle;
    }
    // On an arc only within its sweep; outside it, the nearer end counts.
    double start = std::atan2(curve.y0 - curve.cy, curve.x0 - curve.cx);
    double end = std::atan2(curve.y1 - curve.cy, curve.x1 - curve.cx);
    if (curve.clockwise) {
        std::swap(start, end);
    }
    const double at = std::atan2(v - curve.cy, u - curve.cx);
    if (sweepBetween(start, at) <= sweepBetween(start, end)) {
        return onCircle;
    }
    return std::min(std::hypot(u - curve.x0, v - curve.y0), std::hypot(u - curve.x1, v - curve.y1));
}

// The direction of a curve at (u, v), on the sketch plane.
std::pair<double, double> tangentAt(const ProfileSegment& curve, double u, double v) {
    if (curve.kind == Kind::Line) {
        return {curve.x1 - curve.x0, curve.y1 - curve.y0};
    }
    const double sign = curve.clockwise ? -1.0 : 1.0;
    return {-(v - curve.cy) * sign, (u - curve.cx) * sign};
}

// The curve an edge is a piece of: the nearest one to its middle within `tolerance`.
std::optional<std::size_t> curveOf(const TopoDS_Edge& edge, std::span<const ProfileSegment> curves,
                                   std::span<const std::size_t> candidates,
                                   const SketchFrame& frame, double tolerance) {
    const BRepAdaptor_Curve adaptor(edge);
    const gp_Pnt middle = adaptor.Value((adaptor.FirstParameter() + adaptor.LastParameter()) / 2.0);
    const auto [u, v] = inSketch(frame, middle);
    std::optional<std::size_t> nearest;
    double best = tolerance;
    for (const std::size_t i : candidates) {
        const double distance = distanceTo(curves[i], u, v);
        if (distance <= best) {
            best = distance;
            nearest = i;
        }
    }
    return nearest;
}

}  // namespace

std::vector<SketchRegion> sketchRegions(std::span<const ProfileSegment> curves,
                                        const SketchFrame& frame) {
    std::vector<std::size_t> used;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        if (usable(curves[i])) {
            used.push_back(i);
        }
    }
    if (used.empty()) {
        return {};
    }
    return kernel::guard("Finding the sketch's profiles", [&] {
        // A sheet on the sketch plane well beyond the curves, cut into pieces by them. The piece
        // that touches the sheet's border is outside every region.
        double umin = HUGE_VAL;
        double umax = -HUGE_VAL;
        double vmin = HUGE_VAL;
        double vmax = -HUGE_VAL;
        const auto extend = [&](double u, double v, double r) {
            umin = std::min(umin, u - r);
            umax = std::max(umax, u + r);
            vmin = std::min(vmin, v - r);
            vmax = std::max(vmax, v + r);
        };
        NCollection_List<TopoDS_Shape> tools;
        for (const std::size_t i : used) {
            const ProfileSegment& curve = curves[i];
            if (curve.kind == Kind::Line) {
                extend(curve.x0, curve.y0, 0.0);
                extend(curve.x1, curve.y1, 0.0);
            } else {
                extend(curve.cx, curve.cy, radiusOf(curve));
            }
            tools.Append(curveEdge(curve, frame));
        }
        const double size = std::max({umax - umin, vmax - vmin, 1.0});
        const double margin = size;
        const double tolerance = 1e-6 * size;
        const Vec3 n = frame.normal();
        const gp_Pln plane(gp_Ax3(point(frame.origin), direction(n), direction(frame.xAxis)));
        NCollection_List<TopoDS_Shape> arguments;
        arguments.Append(BRepBuilderAPI_MakeFace(plane, umin - margin, umax + margin, vmin - margin,
                                                 vmax + margin)
                             .Face());

        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(arguments);
        splitter.SetTools(tools);
        const kernel::Progress progress;
        splitter.Build(progress.range());
        progress.check();
        if (!splitter.IsDone() || splitter.HasErrors()) {
            throw Error(ErrorCode::Kernel, "Splitting the sketch plane by its curves failed");
        }

        std::vector<SketchRegion> regions;
        std::vector<std::string> names;
        kernel::ShapeMap faces;
        BRep_Builder builder;
        for (TopExp_Explorer piece(splitter.Shape(), TopAbs_FACE); piece.More(); piece.Next()) {
            // Sides are found on the face as stored; flipping a face flips both its normal and
            // the direction its edges run, so the side stays the same.
            const TopoDS_Face face = TopoDS::Face(piece.Current().Oriented(TopAbs_FORWARD));
            const gp_Dir normal = BRepAdaptor_Surface(face).Plane().Axis().Direction();
            const bool alongNormal = normal.X() * n.x + normal.Y() * n.y + normal.Z() * n.z > 0.0;
            TopoDS_Face region = TopoDS::Face(face.EmptyCopied());
            std::set<std::string> sides;
            bool outside = false;
            for (TopoDS_Iterator wires(face); wires.More() && !outside; wires.Next()) {
                // Curves inside a region that do not split it (an open line) are internal
                // edges; the region leaves them out.
                TopoDS_Wire wire;
                builder.MakeWire(wire);
                bool internal = false;
                bool bounding = false;
                for (TopoDS_Iterator edges(wires.Value()); edges.More(); edges.Next()) {
                    const TopoDS_Edge& edge = TopoDS::Edge(edges.Value());
                    if (edge.Orientation() != TopAbs_FORWARD &&
                        edge.Orientation() != TopAbs_REVERSED) {
                        internal = true;
                        continue;
                    }
                    const auto curve = curveOf(edge, curves, used, frame, tolerance);
                    if (!curve) {
                        outside = true;
                        break;
                    }
                    const BRepAdaptor_Curve adaptor(edge);
                    gp_Pnt middle;
                    gp_Vec along;
                    adaptor.D1((adaptor.FirstParameter() + adaptor.LastParameter()) / 2.0, middle,
                               along);
                    if (edge.Orientation() == TopAbs_REVERSED) {
                        along.Reverse();
                    }
                    const auto [u, v] = inSketch(frame, middle);
                    const auto [tu, tv] = tangentAt(curves[*curve], u, v);
                    const double du = along.X() * frame.xAxis.x + along.Y() * frame.xAxis.y +
                                      along.Z() * frame.xAxis.z;
                    const double dv = along.X() * frame.yAxis.x + along.Y() * frame.yAxis.y +
                                      along.Z() * frame.yAxis.z;
                    // A face lies left of its edges, seen from its normal.
                    const bool left = (du * tu + dv * tv > 0.0) == alongNormal;
                    sides.insert(curves[*curve].name + (left ? "+" : "-"));
                    builder.Add(wire, edge);
                    bounding = true;
                }
                if (bounding) {
                    builder.Add(region, internal ? wire : TopoDS::Wire(wires.Value()));
                }
            }
            if (outside || sides.empty()) {
                continue;
            }
            if (!alongNormal) {
                region.Reverse();
            }
            std::string name = "region{";
            for (auto it = sides.begin(); it != sides.end(); ++it) {
                name += (it == sides.begin() ? "" : "|") + *it;
            }
            name += '}';
            SketchRegion result{
                .name = {}, .face = kernel::wrap(region, "Finding a profile"), .edges = {}};
            const Shape::Impl& data = result.face.impl();
            for (int i = 1; i <= data.edges.Extent(); ++i) {
                const auto curve =
                    curveOf(TopoDS::Edge(data.edges(i)), curves, used, frame, tolerance);
                result.edges.push_back(curve ? curves[*curve].name : std::string());
            }
            faces.Add(region);
            names.push_back(std::move(name));
            regions.push_back(std::move(result));
        }
        disambiguate(faces, names);
        for (std::size_t i = 0; i < regions.size(); ++i) {
            regions[i].name = std::move(names[i]);
        }
        std::sort(regions.begin(), regions.end(),
                  [](const SketchRegion& a, const SketchRegion& b) { return a.name < b.name; });
        return regions;
    });
}

Shape regionsShape(std::span<const SketchRegion> regions) {
    return kernel::guard("Collecting profiles", [&] {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        for (const SketchRegion& region : regions) {
            builder.Add(compound, region.face.impl().shape);
        }
        return kernel::wrap(compound, "Collecting profiles");
    });
}

NamedShape extrudeRegions(std::span<const SketchRegion> regions, const SketchFrame& frame,
                          double distance, std::string_view feature) {
    if (regions.empty()) {
        throw Error(ErrorCode::InvalidArgument, "Choose at least one profile to extrude");
    }
    kernel::requireFinite(distance, "Extrude distance");
    if (std::abs(distance) < kPointTolerance) {
        throw Error(ErrorCode::InvalidArgument, "Extrude distance must not be zero");
    }
    return kernel::guard("Extruding profiles", [&] {
        // The curve behind each edge, and how many chosen regions it bounds. Edges between two
        // chosen regions disappear: adjacent regions merge.
        kernel::ShapeMap edges;
        std::vector<std::string> edgeCurves;
        std::vector<int> uses;
        for (const SketchRegion& region : regions) {
            const Shape::Impl& data = region.face.impl();
            for (int i = 1; i <= data.edges.Extent(); ++i) {
                const int index = edges.Add(data.edges(i));
                if (static_cast<std::size_t>(index) > edgeCurves.size()) {
                    edgeCurves.push_back(region.edges.at(static_cast<std::size_t>(i) - 1));
                    uses.push_back(0);
                }
                ++uses[static_cast<std::size_t>(index) - 1];
            }
        }
        std::vector<TopoDS_Face> faces;
        if (std::all_of(uses.begin(), uses.end(), [](int count) { return count == 1; })) {
            for (const SketchRegion& region : regions) {
                faces.push_back(TopoDS::Face(region.face.impl().shape));
            }
        } else {
            const TopoDS_Face& first = TopoDS::Face(regions.front().face.impl().shape);
            BOPAlgo_BuilderFace merge;
            merge.SetFace(TopoDS::Face(first.Oriented(TopAbs_FORWARD)));
            NCollection_List<TopoDS_Shape> boundary;
            for (const SketchRegion& region : regions) {
                const TopoDS_Shape face = region.face.impl().shape.Oriented(TopAbs_FORWARD);
                for (TopExp_Explorer edge(face, TopAbs_EDGE); edge.More(); edge.Next()) {
                    if (uses[static_cast<std::size_t>(edges.FindIndex(edge.Current())) - 1] == 1) {
                        boundary.Append(edge.Current());
                    }
                }
            }
            merge.SetShapes(boundary);
            merge.Perform();
            if (merge.HasErrors()) {
                throw Error(ErrorCode::Kernel, "Merging the chosen profiles failed");
            }
            for (const TopoDS_Shape& area : merge.Areas()) {
                faces.push_back(TopoDS::Face(area.Oriented(first.Orientation())));
            }
        }

        // Pieces of one curve that meet only each other, for example where another curve
        // touched it or where merged regions met, become one edge again: one side face each.
        for (TopoDS_Face& face : faces) {
            ShapeUpgrade_UnifySameDomain unify(face, /*UnifyEdges=*/true, /*UnifyFaces=*/false,
                                               /*ConcatBSplines=*/false);
            unify.Build();
            for (int i = 1; i <= edges.Extent(); ++i) {
                for (const TopoDS_Shape& merged : unify.History()->Modified(edges(i))) {
                    const int index = edges.Add(merged);
                    if (static_cast<std::size_t>(index) > edgeCurves.size()) {
                        edgeCurves.push_back(edgeCurves[static_cast<std::size_t>(i) - 1]);
                    }
                }
            }
            face = TopoDS::Face(unify.Shape());
        }

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        for (const TopoDS_Face& face : faces) {
            builder.Add(compound, face);
        }
        const TopoDS_Shape base = faces.size() == 1 ? TopoDS_Shape(faces.front()) : compound;
        const Vec3 n = frame.normal();
        BRepPrimAPI_MakePrism prism(base, gp_Vec(n.x * distance, n.y * distance, n.z * distance));
        prism.Build();
        if (!prism.IsDone()) {
            throw Error(ErrorCode::Kernel, "Extruding the profiles failed");
        }
        Shape result = kernel::checkedResult(prism.Shape(), "Extruding profiles");

        FaceNames names(result.impl());
        const std::string prefix(feature);
        for (const TopoDS_Face& face : faces) {
            for (TopExp_Explorer edge(face, TopAbs_EDGE); edge.More(); edge.Next()) {
                const int index = edges.FindIndex(edge.Current());
                if (index > 0) {
                    names.generated(
                        prism, edge.Current(),
                        prefix + ":side[" + edgeCurves[static_cast<std::size_t>(index) - 1] + "]");
                }
            }
        }
        for (TopExp_Explorer cap(prism.FirstShape(), TopAbs_FACE); cap.More(); cap.Next()) {
            names.add(cap.Current(), prefix + ":start");
        }
        for (TopExp_Explorer cap(prism.LastShape(), TopAbs_FACE); cap.More(); cap.Next()) {
            names.add(cap.Current(), prefix + ":end");
        }
        return nameTopology(std::move(result), names.finish(feature));
    });
}

SketchFrame planarFaceFrame(const NamedShape& body, std::uint32_t faceId) {
    const Shape::Impl& data = body.shape.impl();
    if (faceId == 0 || faceId > static_cast<std::uint32_t>(data.faces.Extent())) {
        throw Error(ErrorCode::InvalidArgument,
                    "Face " + std::to_string(faceId) + " does not exist");
    }
    return kernel::guard("Finding a face's plane", [&] {
        const TopoDS_Face& face = TopoDS::Face(data.faces(static_cast<int>(faceId)));
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) {
            throw Error(ErrorCode::InvalidArgument, "Sketches need a planar face");
        }
        const gp_Ax3 position = surface.Plane().Position();
        gp_Dir normal = position.Direction();
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();  // out of the body
        }
        const gp_Dir x = position.XDirection();
        const gp_Dir y = normal.Crossed(x);
        const gp_Pnt origin = position.Location();
        return SketchFrame{.origin = {origin.X(), origin.Y(), origin.Z()},
                           .xAxis = {x.X(), x.Y(), x.Z()},
                           .yAxis = {y.X(), y.Y(), y.Z()}};
    });
}

NamedShape filletEdges(const NamedShape& body, std::span<const std::uint32_t> edgeIds,
                       double radius, std::string_view feature) {
    kernel::requirePositive(radius, "Fillet radius");
    const Shape::Impl& data = body.shape.impl();
    if (edgeIds.empty()) {
        throw Error(ErrorCode::InvalidArgument, "Fillet needs at least one edge");
    }
    const auto edgeCount = static_cast<std::uint32_t>(data.edges.Extent());
    for (const std::uint32_t id : edgeIds) {
        if (id == 0 || id > edgeCount) {
            throw Error(ErrorCode::InvalidArgument, "Edge ID " + std::to_string(id) +
                                                        " does not exist (the shape has " +
                                                        std::to_string(edgeCount) + " edges)");
        }
    }
    return kernel::guard("Filleting edges", [&] {
        BRepFilletAPI_MakeFillet fillet(data.shape);
        for (const std::uint32_t id : edgeIds) {
            fillet.Add(radius, TopoDS::Edge(data.edges(static_cast<int>(id))));
        }
        const kernel::Progress progress;
        fillet.Build(progress.range());
        progress.check();
        if (!fillet.IsDone()) {
            throw Error(ErrorCode::Kernel, "Filleting edges failed: the radius " +
                                               std::to_string(radius) +
                                               " may be too large for the selected edges");
        }
        Shape result = kernel::checkedResult(fillet.Shape(), "Filleting edges");

        FaceNames names(result.impl());
        names.inherit(fillet, body);
        const std::string prefix = std::string(feature) + ":fillet[";
        for (const std::uint32_t id : edgeIds) {
            const auto index = static_cast<int>(id);
            names.generated(fillet, data.edges(index),
                            prefix + body.edges[static_cast<std::size_t>(id) - 1] + "]");
            // Corners where several rounded edges meet get a blend generated from the vertex.
            TopoDS_Vertex first;
            TopoDS_Vertex last;
            TopExp::Vertices(TopoDS::Edge(data.edges(index)), first, last);
            for (const TopoDS_Vertex& vertex : {first, last}) {
                const int vertexId = data.vertices.FindIndex(vertex);
                if (vertexId > 0) {
                    names.generated(
                        fillet, vertex,
                        prefix + body.vertices[static_cast<std::size_t>(vertexId) - 1] + "]");
                }
            }
        }
        return nameTopology(std::move(result), names.finish(feature));
    });
}

}  // namespace facetry::core
