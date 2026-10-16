// Small float math for the viewport: vectors, 4x4 matrices (column-major, like the shaders) and
// rays. Intentionally minimal; the geometry core does its own math in double precision.
#pragma once

#include <array>
#include <cmath>

namespace facetry::render {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend Vec3 operator+(Vec3 a, Vec3 b) {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    friend Vec3 operator-(Vec3 a, Vec3 b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    friend Vec3 operator*(Vec3 a, float s) {
        return {a.x * s, a.y * s, a.z * s};
    }
    friend Vec3 operator*(float s, Vec3 a) {
        return a * s;
    }
    friend Vec3 operator-(Vec3 a) {
        return {-a.x, -a.y, -a.z};
    }
};

inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalize(Vec3 a) {
    const float len = length(a);
    return len > 0.0F ? a * (1.0F / len) : a;
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

// Rotates `v` around the unit axis `axis` by `angle` radians (Rodrigues).
inline Vec3 rotate(Vec3 v, Vec3 axis, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0F - c));
}

// Column-major 4x4 matrix: m[column * 4 + row].
struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity();
    friend Mat4 operator*(const Mat4& a, const Mat4& b);
};

struct Vec4 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

Vec4 transform(const Mat4& matrix, Vec3 point);  // point with w = 1

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);
// `zeroToOneDepth`: clip-space depth range [0, 1] (Metal, WebGPU) instead of [-1, 1] (OpenGL).
Mat4 perspective(float fovY, float aspect, float near, float far, bool zeroToOneDepth);
Mat4 orthographic(float halfWidth, float halfHeight, float near, float far, bool zeroToOneDepth);

struct Ray {
    Vec3 origin;
    Vec3 direction;  // unit length
};

struct Bounds {
    Vec3 min{1e30F, 1e30F, 1e30F};
    Vec3 max{-1e30F, -1e30F, -1e30F};

    [[nodiscard]] bool isEmpty() const {
        return min.x > max.x;
    }
    void add(Vec3 p);
    void merge(const Bounds& other);
    [[nodiscard]] Vec3 center() const {
        return (min + max) * 0.5F;
    }
    [[nodiscard]] float radius() const {
        return isEmpty() ? 0.0F : length(max - min) * 0.5F;
    }
};

// Distance along the ray to the box, or a negative value if it misses.
float intersect(const Ray& ray, const Bounds& box, float maxDistance);

}  // namespace facetry::render
