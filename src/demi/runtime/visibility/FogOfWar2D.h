#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <cstdint>
#include <span>
#include <vector>

namespace demi::runtime {
struct FogBoundaryPoint2D {
  float angle = 0;
  Vec2 point;
};

// Scene-local geometry and exploration state. Pixel coverage, range falloff,
// and memory interpolation belong to the GPU renderer, not simulation.
class FogOfWar2D {
public:
  bool configure(int width, int height, float cellSize, Vec2 origin,
                 Color color, float memoryOpacity);
  void clear();
  void reveal(int x, int y);
  bool update(Vec2 focus, float radius, float feather,
              std::span<const float> rayDistances,
              std::span<const float> rayAngles = {});
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] Vec2 origin() const { return origin_; }
  [[nodiscard]] Vec2 size() const {
    return {width_ * cellSize_, height_ * cellSize_};
  }
  [[nodiscard]] Vec2 focus() const { return focus_; }
  [[nodiscard]] float radius() const { return radius_; }
  [[nodiscard]] float feather() const { return feather_; }
  [[nodiscard]] Color color() const { return color_; }
  [[nodiscard]] float memoryOpacity() const { return memoryOpacity_; }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }
  [[nodiscard]] std::uint64_t discoveryRevision() const {
    return discoveryRevision_;
  }
  [[nodiscard]] std::span<const std::uint8_t> discovered() const {
    return discovered_;
  }
  [[nodiscard]] std::span<const FogBoundaryPoint2D> boundary() const {
    return boundary_;
  }

private:
  int width_ = 0;
  int height_ = 0;
  float cellSize_ = 1;
  Vec2 origin_;
  Vec2 focus_;
  Color color_;
  float radius_ = 0;
  float feather_ = 1;
  float memoryOpacity_ = 0.45F;
  std::uint64_t revision_ = 0;
  std::uint64_t discoveryRevision_ = 0;
  std::vector<std::uint8_t> discovered_;
  std::vector<FogBoundaryPoint2D> boundary_;
};
} // namespace demi::runtime
