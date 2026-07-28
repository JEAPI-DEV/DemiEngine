#include "demi/runtime/navigation/NavigationGrid2D.h"

#include <cmath>
#include <iostream>

using namespace demi::runtime;
using namespace demi::runtime::navigation;

int main() {
  NavigationGrid2D grid;
  if (!grid.configure(6, 5, 2.0F, {-6.0F, -5.0F}) || !grid.available()) {
    std::cerr << "Navigation grid configuration failed.\n";
    return 1;
  }
  for (int y = 0; y < 4; ++y) {
    if (!grid.setBlocked({2, y}, true)) {
      std::cerr << "Dynamic navigation blocker update failed.\n";
      return 1;
    }
  }
  if (!grid.setCost({1, 4}, 8.0F)) {
    std::cerr << "Navigation cost update failed.\n";
    return 1;
  }
  const NavigationPath2D path = grid.path({0, 0}, {5, 0});
  if (!path.success || path.cells.front() != NavigationCell2D{0, 0} ||
      path.cells.back() != NavigationCell2D{5, 0}) {
    std::cerr << "Weighted path request failed: " << path.diagnostic << '\n';
    return 1;
  }
  for (const NavigationCell2D cell : path.cells) {
    if (grid.blocked(cell)) {
      std::cerr << "Path crossed a dynamic blocker.\n";
      return 1;
    }
  }
  const auto world = grid.cellToWorld({0, 0});
  const auto cell = world ? grid.worldToCell(*world) : std::nullopt;
  if (!world || !cell || *cell != NavigationCell2D{0, 0} ||
      std::abs(world->x + 5.0F) > 0.001F ||
      std::abs(world->y + 4.0F) > 0.001F) {
    std::cerr << "Navigation cell/world conversion failed.\n";
    return 1;
  }
  if (grid.path({0, 0}, {2, 0}).diagnostic != "PATH_GOAL_BLOCKED") {
    std::cerr << "Blocked-goal diagnostic was not deterministic.\n";
    return 1;
  }
  return 0;
}
