#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace demi::runtime {

struct MeshRendererComponent {
  static constexpr std::string_view typeName = "MeshRenderer";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string model;
  std::string shape = "cube";
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Color color = {0.8F, 0.8F, 0.8F, 1.0F};
  std::string texture;
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
  std::uint64_t revision = 0;
  Vec3 boundsMin;
  Vec3 boundsMax;
  bool hasBounds = false;
  bool wireframe = false;
};

} // namespace demi::runtime
