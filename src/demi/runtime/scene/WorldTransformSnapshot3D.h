#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

// Owns derived world poses. Transform changes arrive through EnTT signals;
// only affected descendants are recomposed. No component pointers are retained.
class WorldTransformSnapshot3D {
public:
  WorldTransformSnapshot3D();
  ~WorldTransformSnapshot3D();
  WorldTransformSnapshot3D(const WorldTransformSnapshot3D &) = delete;
  WorldTransformSnapshot3D &
  operator=(const WorldTransformSnapshot3D &) = delete;
  WorldTransformSnapshot3D(WorldTransformSnapshot3D &&) noexcept;
  WorldTransformSnapshot3D &operator=(WorldTransformSnapshot3D &&) noexcept;

  // Results retain authored order. Missing parents and cyclic chains yield
  // null.
  const std::vector<std::optional<WorldTransform3D>> &
  refresh(const World &world);

private:
  struct ChangeTracker;
  void rebuildIndices();
  void rebuildParents();
  void resolveAll();
  void resolveChanged();
  void resolveChain(std::size_t index);

  std::unique_ptr<ChangeTracker> changes_;
  std::uint64_t registryIdentity_ = 0;
  std::uint64_t orderRevision_ = 0;
  std::vector<std::string> ids_;
  std::vector<std::optional<Transform3DComponent>> locals_;
  std::unordered_map<std::string, std::size_t> indices_;
  std::vector<std::optional<std::size_t>> parents_;
  std::vector<std::vector<std::size_t>> children_;
  std::vector<std::optional<WorldTransform3D>> results_;
  std::vector<unsigned char> states_;
  std::vector<std::size_t> dirty_;
  std::vector<std::size_t> affected_;
  std::vector<std::size_t> chain_;
};

[[nodiscard]] std::vector<std::optional<WorldTransform3D>>
resolveWorldTransforms3D(const World &world);
} // namespace demi::runtime
