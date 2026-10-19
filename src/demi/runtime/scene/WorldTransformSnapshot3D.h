#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

// Owns a content-checked transform snapshot. No entity/component pointers are
// retained, so replacement worlds and edits with unchanged sizes are safe.
class WorldTransformSnapshot3D {
public:
  // Results retain World entity order. Missing parents and cyclic chains
  // produce null. The returned reference remains valid until the next refresh.
  const std::vector<std::optional<WorldTransform3D>> &refresh(const World &world);

private:
  void rebuildIndices();
  void rebuildParents();
  void resolve();

  std::vector<std::string> ids_;
  std::vector<std::optional<Transform3DComponent>> locals_;
  std::unordered_map<std::string, std::size_t> indices_;
  std::vector<std::optional<std::size_t>> parents_;
  std::vector<std::optional<WorldTransform3D>> results_;
  std::vector<unsigned char> states_;
  std::vector<std::size_t> chain_;
};

// One-shot convenience for callers that do not retain a snapshot.
[[nodiscard]] std::vector<std::optional<WorldTransform3D>>
resolveWorldTransforms3D(const World &world);

} // namespace demi::runtime
