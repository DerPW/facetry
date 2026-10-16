// Implementation of facetry_core.h on top of facetry::core. Every entry point catches all
// exceptions and reports them as a status plus message on the context.
#include "facetry/core/error.hpp"
#include "facetry/core/log.hpp"
#include "facetry/core/mesh.hpp"
#include "facetry/core/modeling.hpp"
#include "facetry/core/shape.hpp"
#include "facetry/core/version.hpp"

#include "facetry_core.h"

#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>

using facetry::core::Error;
using facetry::core::ErrorCode;
using facetry::core::Mesh;
using facetry::core::Shape;

static_assert(static_cast<int>(ErrorCode::InvalidArgument) == FACETRY_ERROR_INVALID_ARGUMENT);
static_assert(static_cast<int>(ErrorCode::InvalidHandle) == FACETRY_ERROR_INVALID_HANDLE);
static_assert(static_cast<int>(ErrorCode::Kernel) == FACETRY_ERROR_KERNEL);
static_assert(static_cast<int>(ErrorCode::Unsupported) == FACETRY_ERROR_UNSUPPORTED);
static_assert(static_cast<int>(ErrorCode::Io) == FACETRY_ERROR_IO);
static_assert(static_cast<int>(ErrorCode::OutOfMemory) == FACETRY_ERROR_OUT_OF_MEMORY);
static_assert(static_cast<int>(ErrorCode::Internal) == FACETRY_ERROR_INTERNAL);
static_assert(sizeof(facetry_shape_properties) == 12 * sizeof(double),
              "facetry_shape_properties must stay a plain array of doubles");

struct facetry_context {
    // Meshes are held by pointer so their buffers never move while a shell reads them.
    using Object = std::variant<Shape, std::unique_ptr<Mesh>>;

    std::mutex mutex;
    std::unordered_map<facetry_handle, Object> objects;
    facetry_handle nextHandle = 1;
    facetry_status lastStatus = FACETRY_OK;
    std::string lastError;

    facetry_handle add(Object object) {
        if (nextHandle == 0) {
            throw Error(ErrorCode::Internal, "Out of handles");
        }
        const facetry_handle handle = nextHandle++;
        objects.emplace(handle, std::move(object));
        return handle;
    }

    template <typename T>
    T& get(facetry_handle handle, const char* kind) {
        const auto it = objects.find(handle);
        if (it == objects.end() || !std::holds_alternative<T>(it->second)) {
            throw Error(ErrorCode::InvalidHandle,
                        "Handle " + std::to_string(handle) + " is not a valid " + kind);
        }
        return std::get<T>(it->second);
    }

    void release(facetry_handle handle, const char* kind, bool isMesh) {
        const auto it = objects.find(handle);
        const bool matches = it != objects.end() &&
                             (isMesh ? std::holds_alternative<std::unique_ptr<Mesh>>(it->second)
                                     : std::holds_alternative<Shape>(it->second));
        if (!matches) {
            throw Error(ErrorCode::InvalidHandle,
                        "Handle " + std::to_string(handle) + " is not a valid " + kind);
        }
        objects.erase(it);
    }
};

namespace {

template <typename Function>
facetry_status run(facetry_context* context, Function&& function) {
    if (context == nullptr) {
        return FACETRY_ERROR_INVALID_ARGUMENT;
    }
    const std::lock_guard lock(context->mutex);
    facetry_status status = FACETRY_OK;
    std::string message;
    try {
        function(*context);
    } catch (const Error& error) {
        status = static_cast<facetry_status>(error.code());
        message = error.what();
    } catch (const std::bad_alloc&) {
        status = FACETRY_ERROR_OUT_OF_MEMORY;
        message = "Out of memory";
    } catch (const std::exception& error) {
        status = FACETRY_ERROR_INTERNAL;
        message = std::string("Internal error: ") + error.what();
    } catch (...) {
        status = FACETRY_ERROR_INTERNAL;
        message = "Internal error: unknown exception";
    }
    context->lastStatus = status;
    context->lastError = std::move(message);
    if (status != FACETRY_OK) {
        facetry::core::log(facetry::core::LogLevel::Debug, context->lastError);
    }
    return status;
}

template <typename T>
T& requireOut(T* pointer, const char* name) {
    if (pointer == nullptr) {
        throw Error(ErrorCode::InvalidArgument, std::string(name) + " must not be null");
    }
    return *pointer;
}

struct LogCallback {
    facetry_log_callback callback = nullptr;
    void* userData = nullptr;
};

}  // namespace

extern "C" {

void facetry_api_version(uint32_t* major, uint32_t* minor) {
    if (major != nullptr) {
        *major = FACETRY_API_VERSION_MAJOR;
    }
    if (minor != nullptr) {
        *minor = FACETRY_API_VERSION_MINOR;
    }
}

const char* facetry_core_version(void) {
    return facetry::core::version().data();
}

const char* facetry_kernel_version(void) {
    return facetry::core::kernelVersion().data();
}

const char* facetry_platform(void) {
    return facetry::core::platform().data();
}

facetry_status facetry_context_create(facetry_context** out_context) {
    if (out_context == nullptr) {
        return FACETRY_ERROR_INVALID_ARGUMENT;
    }
    *out_context = new (std::nothrow) facetry_context();
    return *out_context != nullptr ? FACETRY_OK : FACETRY_ERROR_OUT_OF_MEMORY;
}

void facetry_context_destroy(facetry_context* context) {
    delete context;
}

const char* facetry_last_error_message(const facetry_context* context) {
    return context != nullptr ? context->lastError.c_str() : "Context is null";
}

facetry_status facetry_last_error_status(const facetry_context* context) {
    return context != nullptr ? context->lastStatus : FACETRY_ERROR_INVALID_ARGUMENT;
}

void facetry_set_log_callback(facetry_log_callback callback, void* user_data) {
    if (callback == nullptr) {
        facetry::core::setLogSink({});
        return;
    }
    const LogCallback target{callback, user_data};
    facetry::core::setLogSink([target](facetry::core::LogLevel level, std::string_view message) {
        const std::string text(message);
        target.callback(static_cast<facetry_log_level>(level), text.c_str(), target.userData);
    });
}

void facetry_set_log_level(facetry_log_level level) {
    facetry::core::setLogLevel(static_cast<facetry::core::LogLevel>(level));
}

facetry_status facetry_shape_make_box(facetry_context* context, double dx, double dy, double dz,
                                      facetry_handle* out_shape) {
    return run(context, [&](facetry_context& ctx) {
        auto& out = requireOut(out_shape, "out_shape");
        out = ctx.add(facetry::core::makeBox(dx, dy, dz));
    });
}

facetry_status facetry_shape_make_cylinder(facetry_context* context, double x, double y,
                                           double radius, double height,
                                           facetry_handle* out_shape) {
    return run(context, [&](facetry_context& ctx) {
        auto& out = requireOut(out_shape, "out_shape");
        out = ctx.add(facetry::core::makeCylinder(x, y, radius, height));
    });
}

facetry_status facetry_shape_boolean(facetry_context* context, facetry_boolean_op op,
                                     facetry_handle shape, facetry_handle tool,
                                     facetry_handle* out_shape) {
    return run(context, [&](facetry_context& ctx) {
        auto& out = requireOut(out_shape, "out_shape");
        if (op != FACETRY_BOOLEAN_FUSE && op != FACETRY_BOOLEAN_CUT &&
            op != FACETRY_BOOLEAN_COMMON) {
            throw Error(ErrorCode::InvalidArgument, "Unknown boolean operation");
        }
        const Shape& a = ctx.get<Shape>(shape, "shape");
        const Shape& b = ctx.get<Shape>(tool, "shape");
        out = ctx.add(facetry::core::booleanOp(static_cast<facetry::core::BooleanOp>(op), a, b));
    });
}

facetry_status facetry_shape_fillet(facetry_context* context, facetry_handle shape,
                                    const uint32_t* edge_ids, size_t edge_count, double radius,
                                    facetry_handle* out_shape) {
    return run(context, [&](facetry_context& ctx) {
        auto& out = requireOut(out_shape, "out_shape");
        if (edge_ids == nullptr && edge_count > 0) {
            throw Error(ErrorCode::InvalidArgument, "edge_ids must not be null");
        }
        const Shape& source = ctx.get<Shape>(shape, "shape");
        const std::span<const uint32_t> edges(edge_ids, edge_count);
        out = ctx.add(facetry::core::fillet(source, edges, radius));
    });
}

facetry_status facetry_shape_properties_get(facetry_context* context, facetry_handle shape,
                                            facetry_shape_properties* out_properties) {
    return run(context, [&](facetry_context& ctx) {
        auto& out = requireOut(out_properties, "out_properties");
        const facetry::core::ShapeProperties p = ctx.get<Shape>(shape, "shape").properties();
        out.volume = p.volume;
        out.area = p.area;
        out.bbox_min[0] = p.bounds.min.x;
        out.bbox_min[1] = p.bounds.min.y;
        out.bbox_min[2] = p.bounds.min.z;
        out.bbox_max[0] = p.bounds.max.x;
        out.bbox_max[1] = p.bounds.max.y;
        out.bbox_max[2] = p.bounds.max.z;
        out.solid_count = static_cast<double>(p.counts.solids);
        out.face_count = static_cast<double>(p.counts.faces);
        out.edge_count = static_cast<double>(p.counts.edges);
        out.vertex_count = static_cast<double>(p.counts.vertices);
    });
}

facetry_status facetry_shape_release(facetry_context* context, facetry_handle shape) {
    return run(context, [&](facetry_context& ctx) { ctx.release(shape, "shape", false); });
}

facetry_status facetry_shape_tessellate(facetry_context* context, facetry_handle shape,
                                        double linear_deflection, double angular_deflection,
                                        int relative, facetry_handle* out_mesh) {
    return run(context, [&](facetry_context& ctx) {
        auto& out = requireOut(out_mesh, "out_mesh");
        const facetry::core::TessellationOptions options{
            .linearDeflection = linear_deflection,
            .angularDeflection = angular_deflection,
            .relative = relative != 0,
        };
        auto mesh = std::make_unique<Mesh>(
            facetry::core::tessellate(ctx.get<Shape>(shape, "shape"), options));
        out = ctx.add(std::move(mesh));
    });
}

facetry_status facetry_mesh_buffer(facetry_context* context, facetry_handle mesh,
                                   facetry_mesh_buffer_kind kind, const void** out_data,
                                   size_t* out_count) {
    return run(context, [&](facetry_context& ctx) {
        auto& data = requireOut(out_data, "out_data");
        auto& count = requireOut(out_count, "out_count");
        const Mesh& m = *ctx.get<std::unique_ptr<Mesh>>(mesh, "mesh");
        const auto expose = [&](const auto& vector) {
            data = vector.data();
            count = vector.size();
        };
        switch (kind) {
            case FACETRY_MESH_POSITIONS:
                expose(m.positions);
                return;
            case FACETRY_MESH_NORMALS:
                expose(m.normals);
                return;
            case FACETRY_MESH_TRIANGLES:
                expose(m.triangles);
                return;
            case FACETRY_MESH_TRIANGLE_FACE_IDS:
                expose(m.triangleFaceIds);
                return;
            case FACETRY_MESH_EDGE_POINTS:
                expose(m.edgePoints);
                return;
            case FACETRY_MESH_EDGE_OFFSETS:
                expose(m.edgeOffsets);
                return;
            case FACETRY_MESH_EDGE_IDS:
                expose(m.edgeIds);
                return;
            case FACETRY_MESH_VERTEX_POSITIONS:
                expose(m.vertexPositions);
                return;
            case FACETRY_MESH_VERTEX_IDS:
                expose(m.vertexIds);
                return;
        }
        throw Error(ErrorCode::InvalidArgument, "Unknown mesh buffer kind");
    });
}

facetry_status facetry_mesh_release(facetry_context* context, facetry_handle mesh) {
    return run(context, [&](facetry_context& ctx) { ctx.release(mesh, "mesh", true); });
}

}  // extern "C"
