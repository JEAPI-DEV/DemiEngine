#pragma once

#include "editor/EditorSceneCommand.h"

#include "demi/runtime/scene/ComponentRegistry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace demi::editor {

struct EditorReferenceChoice {
  std::string id;
  std::string label;
};

struct EditorComponentChoice {
  const runtime::scene_loading::ComponentDescriptor *descriptor = nullptr;
  bool compatible = true;
  std::string incompatibility;
};

struct EditorCommonField {
  const runtime::scene_loading::ComponentDescriptor *component = nullptr;
  const runtime::ComponentFieldDescriptor *field = nullptr;
  std::vector<SceneValueTarget> targets;
  nlohmann::json value;
  bool mixed = false;
};

enum class EditorPropertyOrigin {
  Default,
  Inherited,
  Authored,
  Override,
  Missing,
};

struct EditorPropertyPresentation {
  nlohmann::json value;
  EditorPropertyOrigin origin = EditorPropertyOrigin::Missing;
  bool hasValue = false;
  bool requiresExplicitAdd = false;
  bool canReset = false;
};

[[nodiscard]] std::vector<EditorReferenceChoice>
editorReferenceChoices(runtime::ComponentReferenceKind kind,
                       const std::filesystem::path &projectDirectory,
                       const std::filesystem::path &scenePath,
                       const nlohmann::json &scene,
                       std::span<const std::filesystem::path> sources,
                       std::string_view componentName = {});

[[nodiscard]] std::vector<EditorComponentChoice>
editorComponentChoices(const nlohmann::json &entity);

[[nodiscard]] bool editorComponentMatchesSearch(std::string_view query,
                                                std::string_view internalName,
                                                std::string_view displayName,
                                                std::string_view category);

[[nodiscard]] bool editorPropertyMatchesSearch(
    std::string_view query,
    const runtime::scene_loading::ComponentDescriptor &component,
    const runtime::ComponentFieldDescriptor &field);

[[nodiscard]] EditorPropertyPresentation editorPropertyPresentation(
    const runtime::scene_loading::ComponentDescriptor &descriptor,
    const runtime::ComponentFieldDescriptor &field,
    const nlohmann::json &resolvedComponent, bool isPrefabEntity,
    bool hasExplicitValue);

[[nodiscard]] std::string_view
editorPropertyOriginLabel(EditorPropertyOrigin origin);

[[nodiscard]] std::vector<EditorCommonField>
editorCommonFields(const nlohmann::json &scene,
                   std::span<const std::string> entityIds);

} // namespace demi::editor
