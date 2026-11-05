#include "demi/runtime/visibility/FogOfWar2D.h"
#include <atomic>
#include <cmath>
#include <numbers>

namespace demi::runtime {
namespace {
std::atomic<std::uint64_t> nextRevision{1};
}

bool FogOfWar2D::configure(int width, int height, float cellSize, Vec2 origin,
                           Color color, float memoryOpacity) {
  if (width < 1 || height < 1 || width > 256 || height > 256 ||
      !std::isfinite(cellSize) || cellSize <= 0 ||
      !std::isfinite(width * cellSize) || !std::isfinite(height * cellSize) ||
      !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
      !std::isfinite(memoryOpacity) || memoryOpacity < 0 || memoryOpacity > 1 ||
      !std::isfinite(color.r) || !std::isfinite(color.g) ||
      !std::isfinite(color.b)) {
    return false;
  }
  width_ = width;
  height_ = height;
  cellSize_ = cellSize;
  origin_ = origin;
  color_ = color;
  memoryOpacity_ = memoryOpacity;
  discovered_.assign(width * height, 0);
  boundary_.clear();
  radius_ = 0;
  revision_ = nextRevision.fetch_add(1);
  discoveryRevision_ = revision_;
  return true;
}

void FogOfWar2D::clear() { *this = FogOfWar2D{}; }

void FogOfWar2D::reveal(int x, int y) {
  if (x >= 0 && y >= 0 && x < width_ && y < height_) {
    auto &cell = discovered_[y * width_ + x];
    if (cell == 0) {
      cell = 1;
      discoveryRevision_ = nextRevision.fetch_add(1);
    }
  }
}

bool FogOfWar2D::update(Vec2 focus, float radius, float feather,
                        std::span<const float> rayDistances,
                        std::span<const float> rayAngles) {
  constexpr float tau = 2 * std::numbers::pi_v<float>;
  if (discovered_.empty() || !std::isfinite(focus.x) ||
      !std::isfinite(focus.y) || !std::isfinite(radius) || radius <= 0 ||
      !std::isfinite(feather) || feather <= 0 || rayDistances.size() < 3 ||
      rayDistances.size() > 4096 ||
      (!rayAngles.empty() && rayAngles.size() != rayDistances.size())) {
    return false;
  }
  float previousAngle = -1;
  for (std::size_t index = 0; index < rayDistances.size(); ++index) {
    if (!std::isfinite(rayDistances[index]) || rayDistances[index] < 0) {
      return false;
    }
    if (!rayAngles.empty()) {
      const float angle = rayAngles[index];
      if (!std::isfinite(angle) || angle < 0 || angle >= tau ||
          angle <= previousAngle) {
        return false;
      }
      previousAngle = angle;
    }
  }
  focus_ = focus;
  radius_ = radius;
  feather_ = feather;
  boundary_.resize(rayDistances.size());
  for (std::size_t index = 0; index < rayDistances.size(); ++index) {
    const float angle = rayAngles.empty() ? index * tau / rayDistances.size()
                                          : rayAngles[index];
    boundary_[index] = {angle,
                        {rayDistances[index] * std::cos(angle),
                         rayDistances[index] * std::sin(angle)}};
  }
  revision_ = nextRevision.fetch_add(1);
  return true;
}
} // namespace demi::runtime
