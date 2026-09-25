#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include <map>
#include <optional>
#include <string>

namespace demi::runtime {
// Compact rectangular masonry region. Expansion belongs to the shared asset
// authoring path, not the component parser or a game-specific Lua script.
struct Masonry3DComponent {
  static constexpr std::string_view typeName = "Masonry3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"size", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"columns",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               1,
                               true,
                               false,
                               true,
                               true,
                               false,
                               0,
                               false},
      ComponentFieldDescriptor{"rows",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               1,
                               true,
                               false,
                               true,
                               true,
                               false,
                               0,
                               false},
      ComponentFieldDescriptor{"models", ComponentFieldType::Object},
      ComponentFieldDescriptor::assetReference("texture"),
      ComponentFieldDescriptor::assetReference("height_map"),
      ComponentFieldDescriptor{"relief_depth",
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
      ComponentFieldDescriptor{"texture_grid", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"density",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               .001,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1000000,
                               true},
      ComponentFieldDescriptor{"bond_health",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               .000001,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1e30,
                               true},
      ComponentFieldDescriptor{"anchor_below",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0,
                               false,
                               false,
                               true,
                               true,
                               true},
      ComponentFieldDescriptor{"debris_lifetime", ComponentFieldType::Number, false, true, {}, 0, true}.withHelp("Zero keeps physical rubble. Positive seconds makes detached masonry non-colliding and fades it before removal."),
      ComponentFieldDescriptor{"debris_fade", ComponentFieldType::Number, false, true, {}, 0, true}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D", "Masonry 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  Vec3 size{1, 1, .115F};
  int columns = 4, rows = 10;
  std::map<std::string, std::string> models;
  std::string texture;
  std::string heightMap;
  float reliefDepth = .012F;
  Vec2 textureGrid{8, 14};
  float density = 1800, bondHealth = .5F;
  std::optional<float> anchorBelow;
  float debrisLifetime=0,debrisFade=1;
};
} // namespace demi::runtime
