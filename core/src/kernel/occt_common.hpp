// Kernel adapter internals. Only files in core/src/kernel/ include this header, and only they
// include OpenCASCADE headers.
#pragma once

#include "facetry/core/error.hpp"
#include "facetry/core/shape.hpp"

#include <NCollection_IndexedMap.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>

#include <exception>
#include <new>
#include <string>
#include <utility>

namespace facetry::core {

namespace kernel {
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
}  // namespace kernel

class Shape::Impl {
  public:
    explicit Impl(TopoDS_Shape shape);

    TopoDS_Shape shape;
    // Topology numbering: ID n is the n-th entry of these maps (1-based, like OpenCASCADE).
    kernel::ShapeMap faces;
    kernel::ShapeMap edges;
    kernel::ShapeMap vertices;
};

namespace kernel {

// Wraps an OpenCASCADE result. Throws Error(Kernel) for a null shape.
Shape wrap(const TopoDS_Shape& shape, const char* operation);

// Routes OpenCASCADE's messenger output into facetry's log. Called by guard().
void routeMessagesToLog();

// Runs kernel code and turns OpenCASCADE failures into facetry::core::Error, so that only
// facetry errors leave the adapter.
template <typename Function>
auto guard(const char* operation, Function&& function) -> decltype(function()) {
    routeMessagesToLog();
    try {
        return std::forward<Function>(function)();
    } catch (const Error&) {
        throw;
    } catch (const Standard_Failure& failure) {
        std::string message = std::string(operation) + " failed in the geometry kernel";
        const char* detail = failure.what();
        if (detail != nullptr && *detail != '\0') {
            message += ": ";
            message += detail;
        }
        message += " (";
        message += failure.ExceptionType();
        message += ")";
        throw Error(ErrorCode::Kernel, message);
    } catch (const std::bad_alloc&) {
        throw Error(ErrorCode::OutOfMemory, std::string(operation) + " ran out of memory");
    }
}

}  // namespace kernel
}  // namespace facetry::core
