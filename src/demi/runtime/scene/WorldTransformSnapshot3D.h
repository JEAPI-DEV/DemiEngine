#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

class JobSystem;

// Owns a content-checked transform snapshot. Borrowed local addresses are
// checked against the storage epoch before use; values and IDs are checked
// every refresh.
class WorldTransformSnapshot3D {
public:
  // Results retain World entity order. Missing parents and cyclic chains
  // produce null. The returned reference remains valid until the next refresh.
  const std::vector<std::optional<WorldTransform3D>> &
  refresh(const World &world, JobSystem *jobs = nullptr);

private:
  void rebuildIndices();
  void rebuildParents();
  void resolve();

  std::vector<std::string> ids_;
  const Entity *entityData_ = nullptr;
  std::uint64_t storageEpoch_ = 0;
  std::vector<const Transform3DComponent *> localSources_;
  std::vector<std::optional<Transform3DComponent>> locals_;
  std::unordered_map<std::string, std::size_t> indices_;
  std::vector<std::optional<std::size_t>> parents_;
  std::vector<std::optional<WorldTransform3D>> results_;
  struct Changes {
    bool id = false;
    bool local = false;
    bool parent = false;
  };
  std::vector<Changes> changes_;
  std::vector<unsigned char> states_;
  std::vector<std::size_t> chain_;
};

// One-shot convenience for callers that do not retain a snapshot.
[[nodiscard]] std::vector<std::optional<WorldTransform3D>>
resolveWorldTransforms3D(const World &world);

} // namespace demi::runtime
