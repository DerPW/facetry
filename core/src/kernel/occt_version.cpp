// Kernel adapter: the only place in facetry-core that includes OpenCASCADE headers.
#include "facetry/core/version.hpp"

#include <Standard_Version.hxx>

namespace facetry::core {

std::string_view kernelVersion() noexcept {
    return OCC_VERSION_COMPLETE;
}

}  // namespace facetry::core
