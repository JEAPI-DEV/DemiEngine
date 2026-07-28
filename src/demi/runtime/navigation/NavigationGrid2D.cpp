#include "demi/runtime/navigation/NavigationGrid2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace demi::runtime::navigation {

std::size_t
NavigationCell2DHash::operator()(const NavigationCell2D &cell) const noexcept {
  return std::hash<int>{}(cell.x) ^ (std::hash<int>{}(cell.y) << 1U);
}

bool NavigationGrid2D::configure(const int width, const int height,
                                 const float cellSize, const Vec2 origin) {
  if (width <= 0 || height <= 0 || cellSize <= 0.0F)
    return false;
  width_ = width;
  height_ = height;
  cellSize_ = cellSize;
  origin_ = origin;
  blockers_.clear();
  costs_.clear();
  return true;
}

void NavigationGrid2D::clear() {
  width_ = 0;
  height_ = 0;
  blockers_.clear();
  costs_.clear();
}

bool NavigationGrid2D::available() const { return width_ > 0 && height_ > 0; }
int NavigationGrid2D::width() const { return width_; }
int NavigationGrid2D::height() const { return height_; }
float NavigationGrid2D::cellSize() const { return cellSize_; }
Vec2 NavigationGrid2D::origin() const { return origin_; }

bool NavigationGrid2D::contains(const NavigationCell2D cell) const {
  return cell.x >= 0 && cell.y >= 0 && cell.x < width_ && cell.y < height_;
}

bool NavigationGrid2D::setBlocked(const NavigationCell2D cell,
                                  const bool blockedValue) {
  if (!contains(cell))
    return false;
  if (blockedValue)
    blockers_.insert(cell);
  else
    blockers_.erase(cell);
  return true;
}

bool NavigationGrid2D::setCost(const NavigationCell2D cell, const float value) {
  if (!contains(cell) || value < 1.0F)
    return false;
  if (value == 1.0F)
    costs_.erase(cell);
  else
    costs_[cell] = value;
  return true;
}

bool NavigationGrid2D::blocked(const NavigationCell2D cell) const {
  return !contains(cell) || blockers_.contains(cell);
}

float NavigationGrid2D::cost(const NavigationCell2D cell) const {
  if (const auto found = costs_.find(cell); found != costs_.end())
    return found->second;
  return 1.0F;
}

std::optional<NavigationCell2D>
NavigationGrid2D::worldToCell(const Vec2 world) const {
  if (!available())
    return std::nullopt;
  const NavigationCell2D cell{
      .x = static_cast<int>(std::floor((world.x - origin_.x) / cellSize_)),
      .y = static_cast<int>(std::floor((world.y - origin_.y) / cellSize_))};
  return contains(cell) ? std::optional{cell} : std::nullopt;
}

std::optional<Vec2>
NavigationGrid2D::cellToWorld(const NavigationCell2D cell) const {
  if (!contains(cell))
    return std::nullopt;
  return Vec2{origin_.x + (static_cast<float>(cell.x) + 0.5F) * cellSize_,
              origin_.y + (static_cast<float>(cell.y) + 0.5F) * cellSize_};
}

NavigationPath2D NavigationGrid2D::path(const NavigationCell2D start,
                                        const NavigationCell2D goal,
                                        const bool diagonal) const {
  if (!contains(start))
    return {.diagnostic = "PATH_START_OUT_OF_BOUNDS", .cells = {}};
  if (!contains(goal))
    return {.diagnostic = "PATH_GOAL_OUT_OF_BOUNDS", .cells = {}};
  if (blocked(goal))
    return {.diagnostic = "PATH_GOAL_BLOCKED", .cells = {}};

  struct FrontierNode {
    NavigationCell2D cell;
    float priority = 0.0F;
  };
  const auto compare = [](const FrontierNode &left, const FrontierNode &right) {
    return left.priority > right.priority;
  };
  std::priority_queue<FrontierNode, std::vector<FrontierNode>,
                      decltype(compare)>
      frontier(compare);
  std::unordered_map<NavigationCell2D, NavigationCell2D, NavigationCell2DHash>
      previous;
  std::unordered_map<NavigationCell2D, float, NavigationCell2DHash> distance;
  frontier.push({start, 0.0F});
  distance[start] = 0.0F;

  constexpr std::array<NavigationCell2D, 8> directions{{
      {1, 0},
      {-1, 0},
      {0, 1},
      {0, -1},
      {1, 1},
      {1, -1},
      {-1, 1},
      {-1, -1},
  }};
  while (!frontier.empty()) {
    const NavigationCell2D current = frontier.top().cell;
    frontier.pop();
    if (current == goal)
      break;
    const int directionCount = diagonal ? 8 : 4;
    for (int index = 0; index < directionCount; ++index) {
      const NavigationCell2D next{current.x + directions[index].x,
                                  current.y + directions[index].y};
      if (blocked(next))
        continue;
      if (index >= 4 &&
          (blocked({current.x + directions[index].x, current.y}) ||
           blocked({current.x, current.y + directions[index].y})))
        continue;
      const float stepCost = cost(next) * (index >= 4 ? 1.414213562F : 1.0F);
      const float candidate = distance[current] + stepCost;
      if (const auto known = distance.find(next);
          known != distance.end() && candidate >= known->second)
        continue;
      distance[next] = candidate;
      previous[next] = current;
      const float heuristic = static_cast<float>(std::abs(goal.x - next.x) +
                                                 std::abs(goal.y - next.y));
      frontier.push({next, candidate + heuristic});
    }
  }
  if (!distance.contains(goal))
    return {.diagnostic = "PATH_UNREACHABLE", .cells = {}};

  std::vector<NavigationCell2D> cells;
  for (NavigationCell2D cell = goal;; cell = previous.at(cell)) {
    cells.push_back(cell);
    if (cell == start)
      break;
  }
  std::ranges::reverse(cells);
  return {.success = true, .diagnostic = "OK", .cells = std::move(cells)};
}

} // namespace demi::runtime::navigation
