#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
namespace demi::runtime {
struct SurfaceRelief3DComponent {
  static constexpr std::string_view typeName = "SurfaceRelief3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("height_map", true),
      ComponentFieldDescriptor{"depth",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1,
                               true},
      ComponentFieldDescriptor{"height_min",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1,
                               true},
      ComponentFieldDescriptor{"height_max",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1,
                               true},
      ComponentFieldDescriptor{"uv_offset", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"uv_scale", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"segments", ComponentFieldType::Vec2}};
  static constexpr ComponentEditorMetadata editor{"Rendering 3D",
                                                  "Surface Relief 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  std::string heightMap;
  float depth = .012F, heightMin = .45F, heightMax = .9F;
  Vec2 uvOffset{}, uvScale{1, 1}, segments{24, 10};
};
} // namespace demi::runtime
