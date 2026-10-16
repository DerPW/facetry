#include "facetry/core/modeling.hpp"

#include "occt_common.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>

namespace facetry::core {
namespace {

void requirePositive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw Error(ErrorCode::InvalidArgument,
                    std::string(name) + " must be a positive number, got " + std::to_string(value));
    }
}

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw Error(ErrorCode::InvalidArgument, std::string(name) + " must be a finite number");
    }
}

// Results that fail OpenCASCADE's validity check are reported instead of passed on.
Shape checkedResult(const TopoDS_Shape& shape, const char* operation) {
    if (!shape.IsNull() && !BRepCheck_Analyzer(shape).IsValid()) {
        throw Error(ErrorCode::Kernel, std::string(operation) + " produced an invalid shape");
    }
    return kernel::wrap(shape, operation);
}

}  // namespace

Shape makeBox(double dx, double dy, double dz) {
    requirePositive(dx, "Box width (dx)");
    requirePositive(dy, "Box depth (dy)");
    requirePositive(dz, "Box height (dz)");
    return kernel::guard("Creating a box", [&] {
        return kernel::wrap(BRepPrimAPI_MakeBox(dx, dy, dz).Shape(), "Creating a box");
    });
}

Shape makeCylinder(double x, double y, double radius, double height) {
    requireFinite(x, "Cylinder center x");
    requireFinite(y, "Cylinder center y");
    requirePositive(radius, "Cylinder radius");
    requirePositive(height, "Cylinder height");
    return kernel::guard("Creating a cylinder", [&] {
        const gp_Ax2 axis(gp_Pnt(x, y, 0.0), gp::DZ());
        return kernel::wrap(BRepPrimAPI_MakeCylinder(axis, radius, height).Shape(),
                            "Creating a cylinder");
    });
}

Shape booleanOp(BooleanOp op, const Shape& shape, const Shape& tool) {
    const TopoDS_Shape& a = shape.impl().shape;
    const TopoDS_Shape& b = tool.impl().shape;
    const char* name = op == BooleanOp::Fuse  ? "Joining bodies"
                       : op == BooleanOp::Cut ? "Cutting bodies"
                                              : "Intersecting bodies";
    return kernel::guard(name, [&] {
        std::unique_ptr<BRepAlgoAPI_BooleanOperation> algorithm;
        switch (op) {
            case BooleanOp::Fuse:
                algorithm = std::make_unique<BRepAlgoAPI_Fuse>(a, b);
                break;
            case BooleanOp::Cut:
                algorithm = std::make_unique<BRepAlgoAPI_Cut>(a, b);
                break;
            case BooleanOp::Common:
                algorithm = std::make_unique<BRepAlgoAPI_Common>(a, b);
                break;
        }
        if (!algorithm->IsDone() || algorithm->HasErrors()) {
            std::ostringstream report;
            algorithm->DumpErrors(report);
            throw Error(ErrorCode::Kernel, std::string(name) + " failed: " + report.str());
        }
        return checkedResult(algorithm->Shape(), name);
    });
}

Shape fillet(const Shape& shape, std::span<const std::uint32_t> edgeIds, double radius) {
    requirePositive(radius, "Fillet radius");
    const Shape::Impl& data = shape.impl();
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
        BRepFilletAPI_MakeFillet algorithm(data.shape);
        for (const std::uint32_t id : edgeIds) {
            algorithm.Add(radius, TopoDS::Edge(data.edges(static_cast<int>(id))));
        }
        algorithm.Build();
        if (!algorithm.IsDone()) {
            throw Error(ErrorCode::Kernel, "Filleting edges failed: the radius " +
                                               std::to_string(radius) +
                                               " may be too large for the selected edges");
        }
        return checkedResult(algorithm.Shape(), "Filleting edges");
    });
}

std::vector<std::uint32_t> straightEdgesParallelTo(const Shape& shape, Vec3 direction) {
    const double length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                    direction.z * direction.z);
    if (!std::isfinite(length) || length < 1e-12) {
        throw Error(ErrorCode::InvalidArgument, "Direction must not be zero");
    }
    const Shape::Impl& data = shape.impl();
    return kernel::guard("Finding edges", [&] {
        const gp_Dir axis(direction.x, direction.y, direction.z);
        std::vector<std::uint32_t> result;
        for (int i = 1; i <= data.edges.Extent(); ++i) {
            const BRepAdaptor_Curve curve(TopoDS::Edge(data.edges(i)));
            if (curve.GetType() == GeomAbs_Line &&
                curve.Line().Direction().IsParallel(axis, 1e-9)) {
                result.push_back(static_cast<std::uint32_t>(i));
            }
        }
        return result;
    });
}

}  // namespace facetry::core
