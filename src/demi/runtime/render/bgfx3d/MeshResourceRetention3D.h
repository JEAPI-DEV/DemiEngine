#pragma once

#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include <span>
#include <string>
#include <unordered_map>

namespace demi::runtime::render {

// GPU cache ownership follows entity lifetime, independently of camera culling.
// Collect once per updating frame; apply retention after successful submission.
class MeshResourceRetention3D {
public:
  void collect(std::span<const MeshInstancePreparation3D::EntitySource> sources);
  void clear();

  [[nodiscard]] bool containsDynamic(const std::string &id) const;
  [[nodiscard]] bool containsDeformed(const std::string &id) const;

private:
  struct Membership {
    bool dynamic = false;
    bool deformed = false;
  };
  std::unordered_map<std::string, Membership> membership_;
};

} // namespace demi::runtime::render
