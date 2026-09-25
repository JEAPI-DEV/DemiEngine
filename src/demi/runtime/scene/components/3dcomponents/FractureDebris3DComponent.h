#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {
// Optional impact decoration; does not change the structural fracture graph.
struct FractureDebris3DComponent {
  static constexpr std::string_view typeName = "FractureDebris3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{
          "count", ComponentFieldType::Integer, false, true, {}, 0, true}
          .withHelp("Cosmetic chips per accepted spatial impact. Zero disables "
                    "emission."),
      ComponentFieldDescriptor{"max_fragments",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               0,
                               true}
          .withHelp("Live cosmetic chip budget for this emitter. Oldest chips "
                    "are replaced; structural debris is never affected."),
      ComponentFieldDescriptor{
          "lifetime", ComponentFieldType::Number, false, true, {}, 0, true}
          .withHelp("Seconds before cosmetic chips disappear. Zero disables "
                    "emission."),
      ComponentFieldDescriptor{
          "fade_duration", ComponentFieldType::Number, false, true, {}, 0, true}
          .withHelp("Opacity fades during the final seconds of lifetime. Zero "
                    "means immediate removal at expiry."),
      ComponentFieldDescriptor{"size", ComponentFieldType::Vec3}.withHelp(
          "Base chip dimensions in metres, varied deterministically per "
          "fragment."),
      ComponentFieldDescriptor{
          "speed", ComponentFieldType::Number, false, true, {}, 0, true},
      ComponentFieldDescriptor{
          "spin", ComponentFieldType::Number, false, true, {}, 0, true}
          .withHelp("Maximum angular speed in degrees per second per axis."),
      ComponentFieldDescriptor{"gravity", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"color", ComponentFieldType::Color},
      ComponentFieldDescriptor::assetReference("model").withHelp(
          "Optional shared chip model. Omit for built-in cuboid chips; no "
          "generated asset is required."),
      ComponentFieldDescriptor::assetReference("texture"),
      ComponentFieldDescriptor{"render_layer", ComponentFieldType::String},
      ComponentFieldDescriptor{
          "seed", ComponentFieldType::Integer, false, true, {}, 0, true}};
  static constexpr ComponentEditorMetadata editor{"Effects",
                                                  "Fracture Debris 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  int count = 12;
  int maxFragments = 128;
  float lifetime = 3;
  float fadeDuration = 1;
  Vec3 size{0.12F, 0.05F, 0.08F};
  float speed = 2;
  float spin = 180;
  Vec3 gravity{0, -9.81F, 0};
  Color color{0.65F, 0.45F, 0.3F, 1};
  std::string model, texture, renderLayer;
  std::uint32_t seed = 1;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::count>("count"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::maxFragments>("max_fragments"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::lifetime>("lifetime"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::fadeDuration>("fade_duration"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::size>("size"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::speed>("speed"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::spin>("spin"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::gravity>("gravity"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::color>("color"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::model>("model"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::texture>("texture"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::renderLayer>("render_layer"),
      RuntimeFieldBinding<FractureDebris3DComponent>::member<
          &FractureDebris3DComponent::seed>("seed")};
};
} // namespace demi::runtime
