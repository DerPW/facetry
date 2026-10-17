#include "facetry/render/camera.hpp"

#include <algorithm>
#include <cmath>

namespace facetry::render {
namespace {

constexpr Vec3 kWorldUp{0.0F, 0.0F, 1.0F};
// Orbit speed: one viewport height of vertical drag tilts by about 180 degrees.
constexpr float kRadiansPerPoint = 0.006F;
// Orbiting never brings the view direction closer to the poles than this (|forward.z|).
constexpr float kMaxPoleAlignment = 0.995F;
// Tilt of the views straight down and up: enough for a stable "right" axis, too small to see.
constexpr float kPoleTilt = 1e-3F;
// The home view: from the front right, above.
constexpr Vec3 kHomeDirection{1.0F, -1.0F, 0.8F};

// Interpolates between unit vectors along the sphere. Opposite vectors turn around the world Z
// axis (or X), so that front to back turns sideways instead of passing through the target.
Vec3 slerp(Vec3 a, Vec3 b, float t) {
    const float cosine = std::clamp(dot(a, b), -1.0F, 1.0F);
    if (cosine > 0.9995F) {
        return normalize(lerp(a, b, t));
    }
    if (cosine < -0.9995F) {
        Vec3 axis = cross(a, kWorldUp);
        if (length(axis) < 1e-6F) {
            axis = {1.0F, 0.0F, 0.0F};
        }
        return rotate(a, normalize(axis), 3.14159265F * t);
    }
    const float angle = std::acos(cosine);
    const float sine = std::sin(angle);
    return normalize(a * (std::sin((1.0F - t) * angle) / sine) + b * (std::sin(t * angle) / sine));
}

}  // namespace

void Camera::setViewport(float widthPoints, float heightPoints) {
    width_ = std::max(widthPoints, 1.0F);
    height_ = std::max(heightPoints, 1.0F);
}

void Camera::setProjection(Projection projection) {
    if (projection == projection_) {
        return;
    }
    animation_.reset();
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
    animation_.reset();
    eye_ = eye;
    target_ = target;
}

float Camera::fitDistance(float radius) const {
    const float halfFov =
        std::min(fovY_, 2.0F * std::atan(std::tan(fovY_ * 0.5F) * aspect())) * 0.5F;
    return radius / std::sin(halfFov) * 1.05F;
}

void Camera::fit() {
    fit(scene_);
}

void Camera::fit(const Bounds& bounds) {
    animation_.reset();
    const Vec3 center = bounds.isEmpty() ? Vec3{} : bounds.center();
    const float radius = std::max(bounds.radius(), 1e-3F);
    target_ = center;
    eye_ = center - forward() * fitDistance(radius);
    orthoHalfHeight_ = radius * 1.05F * std::max(1.0F, 1.0F / aspect());
}

void Camera::home(float seconds) {
    const Vec3 center = scene_.isEmpty() ? Vec3{} : scene_.center();
    const float radius = std::max(scene_.radius(), 1e-3F);
    moveTo({.target = center,
            .direction = normalize(kHomeDirection),
            .distance = fitDistance(radius),
            .orthoHalfHeight = radius * 1.05F * std::max(1.0F, 1.0F / aspect())},
           seconds);
}

void Camera::viewFrom(Vec3 direction, float seconds) {
    if (length(direction) < 1e-9F) {
        return;
    }
    direction = normalize(direction);
    if (std::abs(direction.z) > 0.9999F) {
        // Straight down or up: tilt toward the front, so that +X points right on the screen.
        direction = normalize(Vec3{0.0F, -kPoleTilt, direction.z > 0.0F ? 1.0F : -1.0F});
    }
    Pose target = animation_ ? animation_->to : pose();
    target.direction = direction;
    moveTo(target, seconds);
}

Camera::Pose Camera::pose() const {
    return {.target = target_,
            .direction = normalize(eye_ - target_),
            .distance = length(eye_ - target_),
            .orthoHalfHeight = orthoHalfHeight_};
}

void Camera::setPose(const Pose& pose) {
    target_ = pose.target;
    eye_ = pose.target + pose.direction * pose.distance;
    orthoHalfHeight_ = pose.orthoHalfHeight;
}

void Camera::moveTo(const Pose& pose, float seconds) {
    if (seconds <= 0.0F) {
        animation_.reset();
        setPose(pose);
        return;
    }
    animation_ = Animation{.from = this->pose(), .to = pose, .elapsed = 0.0F, .duration = seconds};
}

bool Camera::advance(float seconds) {
    if (!animation_) {
        return false;
    }
    Animation& a = *animation_;
    a.elapsed += std::max(seconds, 0.0F);
    const float t = std::min(a.elapsed / a.duration, 1.0F);
    const float s = t * t * (3.0F - 2.0F * t);  // ease in and out
    const Pose to = a.to;
    if (t >= 1.0F) {
        animation_.reset();
        setPose(to);
        return false;
    }
    setPose({.target = lerp(a.from.target, to.target, s),
             .direction = slerp(a.from.direction, to.direction, s),
             .distance = a.from.distance + (to.distance - a.from.distance) * s,
             .orthoHalfHeight =
                 a.from.orthoHalfHeight + (to.orthoHalfHeight - a.from.orthoHalfHeight) * s});
    return true;
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
    animation_.reset();
    // Yaw around the world Z axis through the pivot.
    const float yaw = -dxPoints * kRadiansPerPoint;
    eye_ = pivot + rotate(eye_ - pivot, kWorldUp, yaw);
    target_ = pivot + rotate(target_ - pivot, kWorldUp, yaw);

    // Tilt around the camera's right axis through the pivot, unless that would pass a pole.
    // From a view straight down or up (a named view), tilting away from the pole is allowed.
    const float pitch = -dyPoints * kRadiansPerPoint;
    const Vec3 axis = right();
    const Vec3 newEye = pivot + rotate(eye_ - pivot, axis, pitch);
    const Vec3 newTarget = pivot + rotate(target_ - pivot, axis, pitch);
    const float alignment = std::abs(normalize(newTarget - newEye).z);
    if (alignment < kMaxPoleAlignment || alignment < std::abs(forward().z)) {
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
    animation_.reset();
    const float worldPerPoint = 2.0F * halfHeightAtTarget() / height_;
    const Vec3 delta = right() * (-dxPoints * worldPerPoint) + up() * (dyPoints * worldPerPoint);
    eye_ = eye_ + delta;
    target_ = target_ + delta;
}

void Camera::zoom(float factor, Vec3 anchor) {
    animation_.reset();
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
