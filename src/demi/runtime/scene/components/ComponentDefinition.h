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

enum class ComponentReferenceKind {
  None,
  Asset,
  Entity,
  Prefab,
};

struct ComponentFieldDescriptor {
  [[nodiscard]] static constexpr ComponentFieldDescriptor
  entityReference(std::string_view fieldName, bool fieldRequired = false) {
    return ComponentFieldDescriptor(
        fieldName, ComponentFieldType::String, fieldRequired, true, {}, 0.0,
        false, false, true, true, true, 0.0, false,
        ComponentReferenceKind::Entity);
  }

  [[nodiscard]] static constexpr ComponentFieldDescriptor
  assetReference(std::string_view fieldName, bool fieldRequired = false) {
    return ComponentFieldDescriptor(
        fieldName, ComponentFieldType::String, fieldRequired, true, {}, 0.0,
        false, false, true, true, false, 0.0, false,
        ComponentReferenceKind::Asset);
  }

  constexpr ComponentFieldDescriptor(
      std::string_view fieldName, ComponentFieldType fieldType,
      bool fieldRequired = false, bool fieldEditorVisible = true,
      std::span<const std::string_view> fieldAllowedValues = {},
      double fieldMinimum = 0.0, bool fieldHasMinimum = false,
      bool fieldReplicated = false, bool fieldLuaReadable = true,
      bool fieldLuaWritable = true, bool fieldNullable = false,
      double fieldMaximum = 0.0, bool fieldHasMaximum = false,
      ComponentReferenceKind fieldReferenceKind = ComponentReferenceKind::None,
      std::string_view fieldArrayElementSchema = {},
      std::string_view fieldNestedObjectSchema = {},
      bool fieldRestartRequired = false, bool fieldRuntimeReadOnly = false)
      : name(fieldName), type(fieldType), required(fieldRequired),
        editorVisible(fieldEditorVisible), allowedValues(fieldAllowedValues),
        minimum(fieldMinimum), hasMinimum(fieldHasMinimum),
        replicated(fieldReplicated), luaReadable(fieldLuaReadable),
        luaWritable(fieldLuaWritable), nullable(fieldNullable),
        maximum(fieldMaximum), hasMaximum(fieldHasMaximum),
        referenceKind(fieldReferenceKind),
        arrayElementSchema(fieldArrayElementSchema),
        nestedObjectSchema(fieldNestedObjectSchema),
        restartRequired(fieldRestartRequired),
        runtimeReadOnly(fieldRuntimeReadOnly) {}

  std::string_view name;
  ComponentFieldType type;
  bool required = false;
  bool editorVisible = true;
  std::span<const std::string_view> allowedValues{};
  double minimum = 0.0;
  bool hasMinimum = false;
  bool replicated = false;
  bool luaReadable = true;
  bool luaWritable = true;
  bool nullable = false;
  double maximum = 0.0;
  bool hasMaximum = false;
  ComponentReferenceKind referenceKind = ComponentReferenceKind::None;
  std::string_view arrayElementSchema;
  std::string_view nestedObjectSchema;
  bool restartRequired = false;
  bool runtimeReadOnly = false;
};

struct ComponentEditorMetadata {
  std::string_view category;
  std::string_view displayName;
};

} // namespace demi::runtime
