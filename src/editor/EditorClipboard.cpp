#include "editor/EditorClipboard.h"
#include <stdexcept>

namespace demi::editor {
namespace {
std::string_view kindName(EditorClipboardKind kind) {
  switch (kind) {
  case EditorClipboardKind::Entities:
    return "entities";
  case EditorClipboardKind::Hud:
    return "hud";
  case EditorClipboardKind::TerrainGraph:
    return "terrain_graph";
  }
  throw std::invalid_argument("Unknown editor clipboard kind");
}
} // namespace
std::string encodeEditorClipboard(EditorClipboardKind kind,
                                  nlohmann::json data) {
  return nlohmann::json{{"format_version", 1},
                        {"kind", kindName(kind)},
                        {"data", std::move(data)}}
      .dump();
}

std::optional<EditorClipboardPayload>
decodeEditorClipboard(std::string_view text, std::string &error) {
  error.clear();
  try {
    const auto document = nlohmann::json::parse(text, nullptr, false);
    if (!document.is_object() || document.size() != 3 ||
        !document.at("format_version").is_number_integer() ||
        document.at("format_version") != 1 ||
        !(document.at("data").is_array() || document.at("data").is_object()))
      throw std::invalid_argument(
          "Clipboard does not contain supported authored content.");
    const auto kind = document.at("kind").get<std::string>();
    for (const auto value :
         {EditorClipboardKind::Entities, EditorClipboardKind::Hud,
          EditorClipboardKind::TerrainGraph})
      if (kind == kindName(value))
        return EditorClipboardPayload{value, document.at("data")};
    throw std::invalid_argument("Clipboard content type is not supported.");
  } catch (const std::exception &failure) {
    error = failure.what();
    return {};
  }
}
} // namespace demi::editor
