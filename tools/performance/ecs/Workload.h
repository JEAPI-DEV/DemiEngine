#pragma once

#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include <cstdint>
#include <string>
#include <vector>

namespace demi::evaluation {
using runtime::MeshRendererComponent;
using runtime::Transform3DComponent;

// Flat scenes isolate storage and extraction. Hierarchy propagation and GPU
// submission must be qualified separately before migrating the engine.
struct Identity {
  std::string id;
  std::size_t ordinal;
};

struct RenderRecord {
  float x = 0;
  float y = 0;
  float z = 0;
  float radius = 0;
  bool visible = false;
};

inline Transform3DComponent initialTransform(std::size_t ordinal) {
  Transform3DComponent transform;
  transform.position = {float(ordinal % 400), float(ordinal % 7),
                        float(ordinal / 400)};
  return transform;
}

inline RenderRecord project(const Transform3DComponent *transform,
                            const MeshRendererComponent *mesh) {
  if (!transform || !mesh) {
    return {};
  }
  return {transform->position.x, transform->position.y, transform->position.z,
          mesh->size.x * transform->scale.x, true};
}

// Same five synthetic culling volumes for every backend. This is CPU work only,
// not a claim that these boxes implement the engine's shadow cascade pipeline.
inline std::uint64_t drawChecksum(const std::vector<RenderRecord> &records) {
  std::uint64_t checksum = 0;
  for (int view = 0; view < 5; ++view) {
    const float limit = 80.F * float(view + 1);
    for (std::size_t index = 0; index < records.size(); ++index) {
      const auto &record = records[index];
      if (record.visible && record.x - record.radius <= limit &&
          record.z - record.radius <= limit) {
        checksum += (index + 1) * std::uint64_t(view + 1);
        checksum += std::uint64_t(record.x * 4.F);
      }
    }
  }
  return checksum;
}
} // namespace demi::evaluation
