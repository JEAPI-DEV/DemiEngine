#pragma once

#include "demi/runtime/scene/Component.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <span>
#include <string_view>

namespace demi::runtime {

struct Entity;

enum class ComponentFieldType {
  Boolean,
  Integer,
  Number,
  String,
  Object,
  Vec2,
  Vec3,
  Color,
  Vec2Array,
  Vec3Array,
};

struct ComponentFieldDescriptor {
  std::string_view name;
  ComponentFieldType type;
  bool required = false;
  bool editorVisible = true;
  std::span<const std::string_view> allowedValues{};
  double minimum = 0.0;
  bool hasMinimum = false;
};

struct ComponentEditorMetadata {
  std::string_view category;
  std::string_view displayName;
};

} // namespace demi::runtime
