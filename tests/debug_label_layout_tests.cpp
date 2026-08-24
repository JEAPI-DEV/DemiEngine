#include "demi/runtime/render/bgfx2d/DebugLabelLayout2D.h"

#include <cassert>

namespace {

bool overlaps(const demi::runtime::render::Rect2D &left,
              const demi::runtime::render::Rect2D &right) {
  return left.x < right.x + right.width && left.x + left.width > right.x &&
         left.y < right.y + right.height && left.y + left.height > right.y;
}

} // namespace

int main() {
  using namespace demi::runtime;
  using namespace demi::runtime::render;
  assert(compactDebugLabel2D("short") == "short");
  assert(compactDebugLabel2D("abcdefghijklmnopqrstuvwxyz", 12) ==
         "abcdefghi...");

  std::vector<DebugLabelCandidate2D> crowded;
  for (int index = 0; index < 12; ++index)
    crowded.push_back(
        {.stableId = "entity-" + std::to_string(index),
         .text = "entity-" + std::to_string(index),
         .anchor = {160.0F + (index % 3) * 2.0F, 90.0F + (index % 2) * 2.0F},
         .width = 86.0F,
         .height = 24.0F});
  const auto first = layoutDebugLabels2D(crowded, {640, 360});
  const auto second = layoutDebugLabels2D(crowded, {640, 360});
  assert(first.size() == crowded.size() && second.size() == first.size());
  for (std::size_t index = 0; index < first.size(); ++index) {
    assert(first[index].stableId == second[index].stableId);
    assert(first[index].bounds.x == second[index].bounds.x);
    assert(first[index].bounds.y == second[index].bounds.y);
    assert(first[index].bounds.x >= 0 && first[index].bounds.y >= 0);
    assert(first[index].bounds.x + first[index].bounds.width <= 640);
    assert(first[index].bounds.y + first[index].bounds.height <= 360);
    for (std::size_t other = 0; other < index; ++other)
      assert(!overlaps(first[index].bounds, first[other].bounds));
  }
}
