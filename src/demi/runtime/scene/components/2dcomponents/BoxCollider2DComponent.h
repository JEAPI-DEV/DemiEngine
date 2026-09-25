#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>

namespace demi::runtime {

struct BoxCollider2DComponent {
  static constexpr std::string_view typeName = "BoxCollider2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"size", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"offset", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"is_trigger", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"category_bits", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"mask_bits", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"friction", ComponentFieldType::Number},
      ComponentFieldDescriptor{"restitution", ComponentFieldType::Number},
      ComponentFieldDescriptor{"density", ComponentFieldType::Number},
      ComponentFieldDescriptor{"debug_visible", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 2D",
                                                  "Box Collider 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec2 size = {1.0F, 1.0F};
  Vec2 offset;
  bool isTrigger = false;
  std::string layer;
  std::uint16_t categoryBits = 1;
  std::uint16_t maskBits = 0xFFFF;
  float friction = 0.2F;
  float restitution = 0.0F;
  float density = 1.0F;
  bool debugVisible = true;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::size>("size"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::offset>("offset"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::isTrigger>("is_trigger"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::layer>("layer"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::categoryBits>("category_bits"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::maskBits>("mask_bits"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::friction>("friction"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::restitution>("restitution"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::density>("density"),
      RuntimeFieldBinding<BoxCollider2DComponent>::member<
          &BoxCollider2DComponent::debugVisible>("debug_visible")};
};

} // namespace demi::runtime
