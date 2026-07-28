#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime::navigation {

struct NavigationCell2D {
  int x = 0;
  int y = 0;
  auto operator<=>(const NavigationCell2D &) const = default;
};

struct NavigationCell2DHash {
  std::size_t operator()(const NavigationCell2D &cell) const noexcept;
};

struct NavigationPath2D {
  bool success = false;
  std::string diagnostic;
  std::vector<NavigationCell2D> cells;
};

class NavigationGrid2D {
public:
  bool configure(int width, int height, float cellSize, Vec2 origin = {});
  void clear();
  [[nodiscard]] bool available() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] float cellSize() const;
  [[nodiscard]] Vec2 origin() const;
  [[nodiscard]] bool setBlocked(NavigationCell2D cell, bool blocked);
  [[nodiscard]] bool setCost(NavigationCell2D cell, float cost);
  [[nodiscard]] bool blocked(NavigationCell2D cell) const;
  [[nodiscard]] float cost(NavigationCell2D cell) const;
  [[nodiscard]] std::optional<NavigationCell2D> worldToCell(Vec2 world) const;
  [[nodiscard]] std::optional<Vec2> cellToWorld(NavigationCell2D cell) const;
  [[nodiscard]] NavigationPath2D path(NavigationCell2D start,
                                      NavigationCell2D goal,
                                      bool diagonal = false) const;

private:
  [[nodiscard]] bool contains(NavigationCell2D cell) const;

  int width_ = 0;
  int height_ = 0;
  float cellSize_ = 1.0F;
  Vec2 origin_;
  std::unordered_set<NavigationCell2D, NavigationCell2DHash> blockers_;
  std::unordered_map<NavigationCell2D, float, NavigationCell2DHash> costs_;
};

} // namespace demi::runtime::navigation
