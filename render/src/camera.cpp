#include "facetry/render/camera.hpp"

#include <algorithm>
#include <cmath>

namespace facetry::render {
namespace {

constexpr Vec3 kWorldUp{0.0F, 0.0F, 1.0F};
// Orbit speed: one viewport height of vertical drag tilts by about 180 degrees.
constexpr float kRadiansPerPoint = 0.006F;
// The view direction never gets closer to the poles than this (|forward.z|).
constexpr float kMaxPoleAlignment = 0.995F;

}  // namespace

void Camera::setViewport(float widthPoints, float heightPoints) {
    width_ = std::max(widthPoints, 1.0F);
    height_ = std::max(heightPoints, 1.0F);
}

void Camera::setProjection(Projection projection) {
    if (projection == projection_) {
        return;
    }
    if (projection == Projection::Orthographic) {
        // Keep the apparent size of objects at the target distance.
        orthoHalfHeight_ = length(target_ - eye_) * std::tan(fovY_ * 0.5F);
    } else {
        const float distance = orthoHalfHeight_ / std::tan(fovY_ * 0.5F);
        eye_ = target_ - forward() * distance;
    }
    projection_ = projection;
}

void Camera::setSceneBounds(const Bounds& bounds) {
    scene_ = bounds;
}

void Camera::lookAt(Vec3 eye, Vec3 target) {
    eye_ = eye;
    target_ = target;
}

void Camera::fit() {
    const Vec3 center = scene_.isEmpty() ? Vec3{} : scene_.center();
    const float radius = std::max(scene_.radius(), 1e-3F);
    // Isometric-style default: from the front right, above.
    const Vec3 direction = normalize(Vec3{1.0F, -1.0F, 0.8F});
    const float halfFov =
        std::min(fovY_, 2.0F * std::atan(std::tan(fovY_ * 0.5F) * aspect())) * 0.5F;
    const float distance = radius / std::sin(halfFov) * 1.05F;
    target_ = center;
    eye_ = center + direction * distance;
    orthoHalfHeight_ = radius * 1.05F * std::max(1.0F, 1.0F / aspect());
}

Vec3 Camera::right() const {
    const Vec3 f = forward();
    Vec3 r = cross(f, kWorldUp);
    if (length(r) < 1e-6F) {
        r = {1.0F, 0.0F, 0.0F};
    }
    return normalize(r);
}

void Camera::orbit(float dxPoints, float dyPoints, Vec3 pivot) {
    // Yaw around the world Z axis through the pivot.
    const float yaw = -dxPoints * kRadiansPerPoint;
    eye_ = pivot + rotate(eye_ - pivot, kWorldUp, yaw);
    target_ = pivot + rotate(target_ - pivot, kWorldUp, yaw);

    // Tilt around the camera's right axis through the pivot, unless that would pass a pole.
    const float pitch = -dyPoints * kRadiansPerPoint;
    const Vec3 axis = right();
    const Vec3 newEye = pivot + rotate(eye_ - pivot, axis, pitch);
    const Vec3 newTarget = pivot + rotate(target_ - pivot, axis, pitch);
    if (std::abs(normalize(newTarget - newEye).z) < kMaxPoleAlignment) {
        eye_ = newEye;
        target_ = newTarget;
    }
}

float Camera::halfHeightAtTarget() const {
    if (projection_ == Projection::Orthographic) {
        return orthoHalfHeight_;
    }
    return length(target_ - eye_) * std::tan(fovY_ * 0.5F);
}

void Camera::pan(float dxPoints, float dyPoints) {
    const float worldPerPoint = 2.0F * halfHeightAtTarget() / height_;
    const Vec3 delta = right() * (-dxPoints * worldPerPoint) + up() * (dyPoints * worldPerPoint);
    eye_ = eye_ + delta;
    target_ = target_ + delta;
}

void Camera::zoom(float factor, Vec3 anchor) {
    factor = std::clamp(factor, 0.05F, 20.0F);
    if (projection_ == Projection::Perspective) {
        // Scaling the camera position around the anchor keeps the anchor's screen position.
        const float minDistance = std::max(scene_.radius(), 1e-3F) * 1e-3F;
        if (length(eye_ - anchor) * factor < minDistance) {
            factor = minDistance / std::max(length(eye_ - anchor), 1e-9F);
        }
        eye_ = anchor + (eye_ - anchor) * factor;
        target_ = anchor + (target_ - anchor) * factor;
        return;
    }
    // Orthographic: scale the view size and move parallel to the screen around the anchor.
    orthoHalfHeight_ *= factor;
    const Vec3 f = forward();
    Vec3 offset = target_ - anchor;
    offset = offset - f * dot(offset, f);
    const Vec3 delta = offset * (factor - 1.0F);
    eye_ = eye_ + delta;
    target_ = target_ + delta;
}

void Camera::nearFar(float& near, float& far) const {
    const float radius = std::max(scene_.radius(), 1e-3F);
    const Vec3 center = scene_.isEmpty() ? target_ : scene_.center();
    const float distance = dot(center - eye_, forward());
    if (projection_ == Projection::Orthographic) {
        near = distance - radius * 1.5F;
        far = distance + radius * 1.5F;
        return;
    }
    far = std::max(distance + radius * 1.5F, radius * 1e-2F);
    near = std::max(distance - radius * 1.5F, far * 1e-4F);
}

Mat4 Camera::viewMatrix() const {
    return render::lookAt(eye_, target_, kWorldUp);
}

Mat4 Camera::projectionMatrix(bool zeroToOneDepth) const {
    float near = 0.0F;
    float far = 0.0F;
    nearFar(near, far);
    if (projection_ == Projection::Orthographic) {
        return orthographic(orthoHalfHeight_ * aspect(), orthoHalfHeight_, near, far,
                            zeroToOneDepth);
    }
    return perspective(fovY_, aspect(), near, far, zeroToOneDepth);
}

Ray Camera::rayAt(float xPoints, float yPoints) const {
    const float ndcX = 2.0F * xPoints / width_ - 1.0F;
    const float ndcY = 1.0F - 2.0F * yPoints / height_;
    const Vec3 f = forward();
    const Vec3 r = right();
    const Vec3 u = up();
    if (projection_ == Projection::Orthographic) {
        const Vec3 origin = eye_ + r * (ndcX * orthoHalfHeight_ * aspect()) +
                            u * (ndcY * orthoHalfHeight_) - f * (scene_.radius() * 4.0F + 1.0F);
        return {origin, f};
    }
    const float tanHalf = std::tan(fovY_ * 0.5F);
    const Vec3 direction = normalize(f + r * (ndcX * tanHalf * aspect()) + u * (ndcY * tanHalf));
    return {eye_, direction};
}

Camera::Projected Camera::project(Vec3 world) const {
    const Vec4 clip = transform(viewProjection(false), world);
    if (clip.w <= 1e-9F) {
        return {0.0F, 0.0F, clip.w};
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    return {(ndcX + 1.0F) * 0.5F * width_, (1.0F - ndcY) * 0.5F * height_, clip.w};
}

Vec3 Camera::pointOnTargetPlane(float xPoints, float yPoints) const {
    const Ray ray = rayAt(xPoints, yPoints);
    const Vec3 f = forward();
    const float denominator = dot(ray.direction, f);
    if (std::abs(denominator) < 1e-9F) {
        return target_;
    }
    const float t = dot(target_ - ray.origin, f) / denominator;
    return ray.origin + ray.direction * t;
}

}  // namespace facetry::render
