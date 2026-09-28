#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include <memory>

namespace demi::runtime {
struct HeightField;
struct Terrain3DComponent {
  static constexpr std::string_view typeName = "Terrain3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"recipe", ComponentFieldType::Object}};
  static constexpr ComponentEditorMetadata editor{"3D", "Terrain 3D"};
  nlohmann::json recipe = TerrainRecipe::defaults();
  std::shared_ptr<const HeightField> generated;
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  static bool serializeField(const Terrain3DComponent &, std::string_view,
                             nlohmann::json &);
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Terrain3DComponent>{
          "recipe",
          [](Terrain3DComponent &to, const Terrain3DComponent &from) {
            to.recipe = from.recipe;
            to.generated.reset();
          },
          [](const Terrain3DComponent &from, nlohmann::json &out) {
            out = from.recipe;
            return true;
          }}};
};
} // namespace demi::runtime
