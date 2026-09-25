#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>

namespace demi::runtime {

struct CapsuleCollider2DComponent {
  static constexpr std::string_view typeName = "CapsuleCollider2D";
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
                                                  "Capsule Collider 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec2 size = {1.0F, 2.0F};
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
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::size>("size"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::offset>("offset"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::isTrigger>("is_trigger"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::layer>("layer"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::categoryBits>("category_bits"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::maskBits>("mask_bits"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::friction>("friction"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::restitution>("restitution"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::density>("density"),
      RuntimeFieldBinding<CapsuleCollider2DComponent>::member<
          &CapsuleCollider2DComponent::debugVisible>("debug_visible")};
};

} // namespace demi::runtime
