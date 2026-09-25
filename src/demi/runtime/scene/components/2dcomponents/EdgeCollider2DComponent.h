#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace demi::runtime {

struct EdgeCollider2DComponent {
  static constexpr std::string_view typeName = "EdgeCollider2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"points", ComponentFieldType::Vec2Array, true},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"is_trigger", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"category_bits", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"mask_bits", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"friction", ComponentFieldType::Number},
      ComponentFieldDescriptor{"restitution", ComponentFieldType::Number},
      ComponentFieldDescriptor{"density", ComponentFieldType::Number},
      ComponentFieldDescriptor{"debug_visible", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 2D",
                                                  "Edge/Chain Collider 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::vector<Vec2> points;
  bool loop = false;
  bool isTrigger = false;
  std::string layer;
  std::uint16_t categoryBits = 1;
  std::uint16_t maskBits = 0xFFFF;
  float friction = 0.2F;
  float restitution = 0.0F;
  float density = 0.0F;
  bool debugVisible = true;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::points>("points"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::loop>("loop"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::isTrigger>("is_trigger"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::layer>("layer"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::categoryBits>("category_bits"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::maskBits>("mask_bits"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::friction>("friction"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::restitution>("restitution"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::density>("density"),
      RuntimeFieldBinding<EdgeCollider2DComponent>::member<
          &EdgeCollider2DComponent::debugVisible>("debug_visible")};
};

} // namespace demi::runtime
