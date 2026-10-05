#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace demi::editor {
enum class EditorClipboardKind { Entities, Hud, TerrainGraph };
struct EditorClipboardPayload {
  EditorClipboardKind kind;
  nlohmann::json data;
};
std::string encodeEditorClipboard(EditorClipboardKind kind,
                                  nlohmann::json data);
std::optional<EditorClipboardPayload>
decodeEditorClipboard(std::string_view text, std::string &error);
} // namespace demi::editor
