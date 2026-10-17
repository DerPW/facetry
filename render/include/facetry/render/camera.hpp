#pragma once

#include "facetry/render/math.hpp"

#include <optional>

namespace facetry::render {

// Viewport camera for a Z-up world. Screen coordinates are in points (UIKit points, CSS pixels)
// with the origin in the top-left corner.
class Camera {
  public:
    enum class Projection { Perspective, Orthographic };

    void setViewport(float widthPoints, float heightPoints);
    [[nodiscard]] float viewportWidth() const {
        return width_;
    }
    [[nodiscard]] float viewportHeight() const {
        return height_;
    }

    void setProjection(Projection projection);
    [[nodiscard]] Projection projection() const {
        return projection_;
    }

    // Scene bounds, used for the near and far planes and by fit().
    void setSceneBounds(const Bounds& bounds);

    // Frames the whole scene, or `bounds`, from the current view direction (Fusion's Fit).
    void fit();
    void fit(const Bounds& bounds);
    // The home view: the whole scene from the front right, above.
    void home(float seconds = 0.0F);
    // Turns the camera around its target to look from `direction` (from the target toward the
    // eye), keeping the distance. Views straight down or up get a tilt too small to see, so the
    // front stays at the bottom of the screen. With `seconds` > 0 the turn is animated.
    void viewFrom(Vec3 direction, float seconds = 0.0F);
    // Moves a running animation on. Returns true while one runs. Every other camera move
    // stops it.
    bool advance(float seconds);
    [[nodiscard]] bool animating() const {
        return animation_.has_value();
    }

    // Turntable orbit around `pivot`: horizontal drags rotate around the world Z axis, vertical
    // drags tilt around the camera's right axis. The view never flips over the poles.
    void orbit(float dxPoints, float dyPoints, Vec3 pivot);
    // Moves the camera parallel to the screen so the scene follows the pointer.
    void pan(float dxPoints, float dyPoints);
    // factor < 1 zooms in. `anchor` stays under the same screen position.
    void zoom(float factor, Vec3 anchor);

    [[nodiscard]] Ray rayAt(float xPoints, float yPoints) const;
    // Screen position (points) and clip-space w of a world point; w <= 0 means behind the camera.
    struct Projected {
        float x = 0.0F;
        float y = 0.0F;
        float w = 0.0F;
    };
    [[nodiscard]] Projected project(Vec3 world) const;

    // Point on the plane through the camera target, facing the camera, under a screen position.
    [[nodiscard]] Vec3 pointOnTargetPlane(float xPoints, float yPoints) const;

    [[nodiscard]] Mat4 viewMatrix() const;
    [[nodiscard]] Mat4 projectionMatrix(bool zeroToOneDepth) const;
    [[nodiscard]] Mat4 viewProjection(bool zeroToOneDepth) const {
        return projectionMatrix(zeroToOneDepth) * viewMatrix();
    }

    [[nodiscard]] Vec3 eye() const {
        return eye_;
    }
    [[nodiscard]] Vec3 target() const {
        return target_;
    }
    [[nodiscard]] Vec3 forward() const {
        return normalize(target_ - eye_);
    }
    [[nodiscard]] Vec3 right() const;
    [[nodiscard]] Vec3 up() const {
        return cross(right(), forward());
    }

    void lookAt(Vec3 eye, Vec3 target);

  private:
    struct Pose {
        Vec3 target;
        Vec3 direction;  // unit, from the target toward the eye
        float distance = 1.0F;
        float orthoHalfHeight = 1.0F;
    };
    struct Animation {
        Pose from;
        Pose to;
        float elapsed = 0.0F;
        float duration = 0.0F;
    };

    [[nodiscard]] Pose pose() const;
    void setPose(const Pose& pose);
    void moveTo(const Pose& pose, float seconds);
    // Distance and orthographic size that frame a sphere of `radius`.
    [[nodiscard]] float fitDistance(float radius) const;
    [[nodiscard]] float aspect() const {
        return width_ / height_;
    }
    [[nodiscard]] float halfHeightAtTarget() const;
    void nearFar(float& near, float& far) const;

    float width_ = 1.0F;
    float height_ = 1.0F;
    Projection projection_ = Projection::Perspective;
    float fovY_ = 0.5235988F;  // 30 degrees, a typical CAD field of view
    float orthoHalfHeight_ = 1.0F;
    Vec3 eye_{10.0F, -10.0F, 10.0F};
    Vec3 target_{0.0F, 0.0F, 0.0F};
    Bounds scene_;
    std::optional<Animation> animation_;
};

}  // namespace facetry::render
