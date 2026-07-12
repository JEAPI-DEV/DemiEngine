#pragma once

#include "demi/runtime/scene/model/World.h"

#include <tuple>

namespace demi::runtime {

template <typename Visitor>
bool visitHudElement(World &world, const std::string &id, Visitor &&visitor) {
  bool visited = false;
  auto visitCollection = [&](auto &elements) {
    if (visited)
      return;
    for (auto &element : elements) {
      if (element.id == id) {
        visitor(element);
        visited = true;
        return;
      }
    }
  };
  std::apply([&](auto &...collections) { (visitCollection(collections), ...); },
             std::tie(world.hudText, world.hudRects, world.hudPanels,
                      world.hudCircles, world.hudImages, world.hudButtons));
  return visited;
}

template <typename Visitor>
bool visitHudGroup(World &world, const std::string &group, Visitor &&visitor) {
  bool visited = false;
  auto visitCollection = [&](auto &elements) {
    for (auto &element : elements) {
      if (element.group == group) {
        visitor(element);
        visited = true;
      }
    }
  };
  std::apply([&](auto &...collections) { (visitCollection(collections), ...); },
             std::tie(world.hudText, world.hudRects, world.hudPanels,
                      world.hudCircles, world.hudImages, world.hudButtons));
  return visited;
}

} // namespace demi::runtime
