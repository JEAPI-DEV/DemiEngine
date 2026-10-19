#pragma once
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include <map>
#include <tuple>

namespace demi::runtime::render {
// Presentation-local surface equivalence for plain primitive entities. Mesh
// size belongs to each copy's model matrix and cannot split a shading group.
class PrimitiveSurfacePreparation3D {
public:
  struct Surface {
    std::string shape;
    std::string texture;
    std::array<float, 4> tint;
    std::array<float, 4> metalRough;
  };
  void reset();
  std::optional<std::size_t>
  surfaceFor(const MeshInstancePreparation3D::EntitySource &source);
  const Surface &surface(std::size_t group) const {
    return surfaces_.at(group);
  }
  std::size_t size() const { return surfaces_.size(); }

private:
  using Key =
      std::tuple<std::string, std::string, std::array<std::uint32_t, 8>>;
  std::map<Key, std::size_t> groups_;
  std::unordered_map<const MeshInstancePreparation3D::EntitySource *,
                     std::size_t>
      sources_;
  std::vector<Surface> surfaces_;
};
} // namespace demi::runtime::render
