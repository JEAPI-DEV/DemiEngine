#pragma once

#include <string>
#include <vector>

namespace demi::runtime {

struct Entity;

struct RenderBatch3D {
  std::string key;
  std::vector<Entity *> entities;
};

[[nodiscard]] std::vector<RenderBatch3D>
buildRenderBatches3D(const std::vector<Entity *> &visibleEntities);

} // namespace demi::runtime
