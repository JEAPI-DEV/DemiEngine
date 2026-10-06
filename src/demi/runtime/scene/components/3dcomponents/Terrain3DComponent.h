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
      ComponentFieldDescriptor::assetReference("asset"),
      ComponentFieldDescriptor{"recipe", ComponentFieldType::Object}};
  static constexpr ComponentEditorMetadata editor{"3D", "Terrain 3D"};
  std::string asset;
  // Inline authored recipe for procedural terrain, or the editable source
  // resolved by the asset loader. Resolved recipes never serialize into scenes.
  nlohmann::json recipe;
  std::shared_ptr<const HeightField> generated;
  static void parse(const nlohmann::json &, Entity &);
  static bool validateAuthored(const nlohmann::json &, std::string &error);
  static nlohmann::json schemaConstraints();
  static nlohmann::json defaults();
  static bool serializeField(const Terrain3DComponent &, std::string_view,
                             nlohmann::json &);
  // A generic recipe edit invalidates the retained field, so the next update
  // falls back to full generation instead of a local replay.
  static void copyRecipe(Terrain3DComponent &destination,
                         const Terrain3DComponent &source);
  static void copyAsset(Terrain3DComponent &destination,
                        const Terrain3DComponent &source);
  static bool readRecipe(const Terrain3DComponent &source, nlohmann::json &out);
  static bool readAsset(const Terrain3DComponent &source, nlohmann::json &out);
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Terrain3DComponent>{"asset", copyAsset, readAsset},
      RuntimeFieldBinding<Terrain3DComponent>{"recipe", copyRecipe, readRecipe}
          .withoutDefault()};
};
} // namespace demi::runtime
