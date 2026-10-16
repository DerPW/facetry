#include "occt_common.hpp"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>

namespace facetry::core {

const Shape::Impl& Shape::impl() const {
    if (!impl_) {
        throw Error(ErrorCode::InvalidArgument, "Shape is null");
    }
    return *impl_;
}

TopologyCounts Shape::topologyCounts() const {
    const Impl& data = impl();
    kernel::ShapeMap solids;
    TopExp::MapShapes(data.shape, TopAbs_SOLID, solids);
    return {
        .solids = static_cast<std::size_t>(solids.Extent()),
        .faces = static_cast<std::size_t>(data.faces.Extent()),
        .edges = static_cast<std::size_t>(data.edges.Extent()),
        .vertices = static_cast<std::size_t>(data.vertices.Extent()),
    };
}

ShapeProperties Shape::properties() const {
    const Impl& data = impl();
    return kernel::guard("Measuring the shape", [&] {
        ShapeProperties result;
        result.counts = topologyCounts();

        GProp_GProps volume;
        BRepGProp::VolumeProperties(data.shape, volume);
        result.volume = volume.Mass();

        GProp_GProps surface;
        BRepGProp::SurfaceProperties(data.shape, surface);
        result.area = surface.Mass();

        Bnd_Box box;
        BRepBndLib::AddOptimal(data.shape, box, /*useTriangulation=*/false,
                               /*useShapeTolerance=*/false);
        if (!box.IsVoid()) {
            box.Get(result.bounds.min.x, result.bounds.min.y, result.bounds.min.z,
                    result.bounds.max.x, result.bounds.max.y, result.bounds.max.z);
        }
        return result;
    });
}

}  // namespace facetry::core
