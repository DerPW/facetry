#include "facetry/render/math.hpp"

#include <algorithm>
#include <limits>

namespace facetry::render {

Mat4 Mat4::identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0F;
    return r;
}

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (std::size_t col = 0; col < 4; ++col) {
        for (std::size_t row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

Vec4 transform(const Mat4& a, Vec3 p) {
    const auto& m = a.m;
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14],
            m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15]};
}

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

Mat4 perspective(float fovY, float aspect, float near, float far, bool zeroToOneDepth) {
    const float f = 1.0F / std::tan(fovY * 0.5F);
    Mat4 r;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[11] = -1.0F;
    if (zeroToOneDepth) {
        r.m[10] = far / (near - far);
        r.m[14] = near * far / (near - far);
    } else {
        r.m[10] = (far + near) / (near - far);
        r.m[14] = 2.0F * far * near / (near - far);
    }
    return r;
}

Mat4 orthographic(float halfWidth, float halfHeight, float near, float far, bool zeroToOneDepth) {
    Mat4 r = Mat4::identity();
    r.m[0] = 1.0F / halfWidth;
    r.m[5] = 1.0F / halfHeight;
    if (zeroToOneDepth) {
        r.m[10] = 1.0F / (near - far);
        r.m[14] = near / (near - far);
    } else {
        r.m[10] = 2.0F / (near - far);
        r.m[14] = (far + near) / (near - far);
    }
    return r;
}

void Bounds::add(Vec3 p) {
    min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
    max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
}

void Bounds::merge(const Bounds& other) {
    if (!other.isEmpty()) {
        add(other.min);
        add(other.max);
    }
}

float intersect(const Ray& ray, const Bounds& box, float maxDistance) {
    float tNear = 0.0F;
    float tFar = maxDistance;
    const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float direction[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z};
    const float hi[3] = {box.max.x, box.max.y, box.max.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-12F) {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
                return -1.0F;
            }
            continue;
        }
        const float inverse = 1.0F / direction[axis];
        float t0 = (lo[axis] - origin[axis]) * inverse;
        float t1 = (hi[axis] - origin[axis]) * inverse;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        tNear = std::max(tNear, t0);
        tFar = std::min(tFar, t1);
        if (tNear > tFar) {
            return -1.0F;
        }
    }
    return tNear;
}

}  // namespace facetry::render
