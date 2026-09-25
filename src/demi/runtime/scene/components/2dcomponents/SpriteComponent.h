#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct SpriteComponent {
  static constexpr std::string_view typeName = "Sprite";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array<std::string_view, 3> shapes{"rectangle", "circle",
                                                          "triangle"};
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("texture"),
      ComponentFieldDescriptor{"shape", ComponentFieldType::String, false, true,
                               shapes},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"sorting_order", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"source_position", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"source_size", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"source_normalized",
                               ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"size", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"pivot", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"nine_slice", ComponentFieldType::Vec2Array},
      ComponentFieldDescriptor{"mask_offset", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"mask_size", ComponentFieldType::Vec2},
      ComponentFieldDescriptor::assetReference("material"),
      ComponentFieldDescriptor{"flip_x",
                               ComponentFieldType::Boolean,
                               false,
                               true,
                               {},
                               0.0,
                               false,
                               true},
      ComponentFieldDescriptor{"flip_y",
                               ComponentFieldType::Boolean,
                               false,
                               true,
                               {},
                               0.0,
                               false,
                               true},
      ComponentFieldDescriptor{"color",
                               ComponentFieldType::Color,
                               false,
                               true,
                               {},
                               0.0,
                               false,
                               true}};
  static constexpr ComponentEditorMetadata editor{"2D", "Sprite"};
  static void parse(const nlohmann::json &json, Entity &entity);
  static bool serializeField(const SpriteComponent &component,
                             std::string_view field, nlohmann::json &out);

  std::string texture;
  std::string shape = "rectangle";
  std::string layer;
  int sortingOrder = 0;
  Vec2 sourcePosition{};
  Vec2 sourceSize{};
  bool sourceNormalized = false;
  Vec2 size{};
  Vec2 pivot{0.5F, 0.5F};
  Vec2 sliceStart{};
  Vec2 sliceEnd{};
  Vec2 maskOffset{};
  Vec2 maskSize{};
  std::string material;
  bool flipX = false;
  bool flipY = false;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::texture>(
          "texture"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::shape>(
          "shape"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::layer>(
          "layer"),
      RuntimeFieldBinding<SpriteComponent>::member<
          &SpriteComponent::sortingOrder>("sorting_order"),
      RuntimeFieldBinding<SpriteComponent>::member<
          &SpriteComponent::sourcePosition>("source_position"),
      RuntimeFieldBinding<SpriteComponent>::member<
          &SpriteComponent::sourceSize>("source_size"),
      RuntimeFieldBinding<SpriteComponent>::member<
          &SpriteComponent::sourceNormalized>("source_normalized"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::size>(
          "size"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::pivot>(
          "pivot"),
      RuntimeFieldBinding<SpriteComponent>::members<
          &SpriteComponent::sliceStart, &SpriteComponent::sliceEnd>("nine_slice"),
      RuntimeFieldBinding<SpriteComponent>::member<
          &SpriteComponent::maskOffset>("mask_offset"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::maskSize>(
          "mask_size"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::material>(
          "material"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::flipX>(
          "flip_x"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::flipY>(
          "flip_y"),
      RuntimeFieldBinding<SpriteComponent>::member<&SpriteComponent::color>(
          "color")};
};

} // namespace demi::runtime
