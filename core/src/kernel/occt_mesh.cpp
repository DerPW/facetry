#include "facetry/core/log.hpp"
#include "facetry/core/mesh.hpp"

#include "occt_common.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>

#include <cmath>
#include <string>

namespace facetry::core {
namespace {

void pushPoint(std::vector<float>& out, const gp_XYZ& p) {
    out.push_back(static_cast<float>(p.X()));
    out.push_back(static_cast<float>(p.Y()));
    out.push_back(static_cast<float>(p.Z()));
}

void requireValid(const TessellationOptions& options) {
    if (!std::isfinite(options.linearDeflection) || options.linearDeflection <= 0.0 ||
        !std::isfinite(options.angularDeflection) || options.angularDeflection <= 0.0) {
        throw Error(ErrorCode::InvalidArgument,
                    "Tessellation deflections must be positive numbers");
    }
}

// Absolute linear deflection, needed for edges that have no triangulation to follow.
double absoluteDeflection(const TopoDS_Shape& shape, const TessellationOptions& options) {
    if (!options.relative) {
        return options.linearDeflection;
    }
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    const double diagonal = box.IsVoid() ? 1.0 : std::sqrt(box.SquareExtent());
    return options.linearDeflection * diagonal;
}

void appendFaces(const Shape::Impl& data, Mesh& mesh) {
    for (int faceId = 1; faceId <= data.faces.Extent(); ++faceId) {
        const TopoDS_Face& face = TopoDS::Face(data.faces(faceId));
        TopLoc_Location location;
        const Handle(Poly_Triangulation) & triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) {
            log(LogLevel::Warning, "Face " + std::to_string(faceId) + " has no triangulation");
            continue;
        }
        if (!triangulation->HasNormals()) {
            BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);
        }
        const gp_Trsf transform = location.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const auto first = static_cast<std::uint32_t>(mesh.meshVertexCount());

        for (int node = 1; node <= triangulation->NbNodes(); ++node) {
            pushPoint(mesh.positions, triangulation->Node(node).Transformed(transform).XYZ());
            gp_Dir normal = triangulation->Normal(node).Transformed(transform);
            if (reversed) {
                normal.Reverse();
            }
            pushPoint(mesh.normals, normal.XYZ());
        }
        for (int t = 1; t <= triangulation->NbTriangles(); ++t) {
            int a = 0;
            int b = 0;
            int c = 0;
            triangulation->Triangle(t).Get(a, b, c);
            if (reversed) {
                std::swap(b, c);
            }
            for (const int index : {a, b, c}) {
                mesh.triangles.push_back(first + static_cast<std::uint32_t>(index - 1));
            }
            mesh.triangleFaceIds.push_back(static_cast<std::uint32_t>(faceId));
        }
    }
}

// Edge polylines follow the face triangulation where possible, so lines and shading meet
// without gaps. Edges without an adjacent triangulated face are sampled from their curve.
void appendEdges(const Shape::Impl& data, const TessellationOptions& options, Mesh& mesh) {
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>,
                               TopTools_ShapeMapHasher>
        edgeFaces;
    TopExp::MapShapesAndAncestors(data.shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const double deflection = absoluteDeflection(data.shape, options);

    for (int edgeId = 1; edgeId <= data.edges.Extent(); ++edgeId) {
        const TopoDS_Edge& edge = TopoDS::Edge(data.edges(edgeId));
        if (BRep_Tool::Degenerated(edge)) {
            continue;
        }
        bool sampled = false;
        const int index = edgeFaces.FindIndex(edge);
        if (index > 0) {
            for (const TopoDS_Shape& faceShape : edgeFaces(index)) {
                TopLoc_Location location;
                const Handle(Poly_Triangulation) & triangulation =
                    BRep_Tool::Triangulation(TopoDS::Face(faceShape), location);
                if (triangulation.IsNull()) {
                    continue;
                }
                const Handle(Poly_PolygonOnTriangulation) & polygon =
                    BRep_Tool::PolygonOnTriangulation(edge, triangulation, location);
                if (polygon.IsNull()) {
                    continue;
                }
                const gp_Trsf transform = location.Transformation();
                for (int k = 1; k <= polygon->NbNodes(); ++k) {
                    pushPoint(mesh.edgePoints,
                              triangulation->Node(polygon->Node(k)).Transformed(transform).XYZ());
                }
                sampled = true;
                break;
            }
        }
        if (!sampled) {
            const BRepAdaptor_Curve curve(edge);
            const GCPnts_TangentialDeflection sampler(curve, options.angularDeflection, deflection);
            for (int k = 1; k <= sampler.NbPoints(); ++k) {
                pushPoint(mesh.edgePoints, sampler.Value(k).XYZ());
            }
        }
        mesh.edgeOffsets.push_back(static_cast<std::uint32_t>(mesh.edgePoints.size() / 3));
        mesh.edgeIds.push_back(static_cast<std::uint32_t>(edgeId));
    }
}

void appendVertices(const Shape::Impl& data, Mesh& mesh) {
    for (int vertexId = 1; vertexId <= data.vertices.Extent(); ++vertexId) {
        pushPoint(mesh.vertexPositions,
                  BRep_Tool::Pnt(TopoDS::Vertex(data.vertices(vertexId))).XYZ());
        mesh.vertexIds.push_back(static_cast<std::uint32_t>(vertexId));
    }
}

}  // namespace

Mesh tessellate(const Shape& shape, const TessellationOptions& options) {
    requireValid(options);
    const Shape::Impl& data = shape.impl();
    return kernel::guard("Tessellating the shape", [&] {
        // OCCT stores the triangulation on the shape; meshing is idempotent for equal settings.
        const BRepMesh_IncrementalMesh mesher(data.shape, options.linearDeflection,
                                              options.relative, options.angularDeflection,
                                              /*isInParallel=*/false);
        if (!mesher.IsDone()) {
            throw Error(ErrorCode::Kernel, "Tessellating the shape failed");
        }
        Mesh mesh;
        appendFaces(data, mesh);
        appendEdges(data, options, mesh);
        appendVertices(data, mesh);
        return mesh;
    });
}

}  // namespace facetry::core
