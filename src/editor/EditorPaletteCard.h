#pragma once

#include "editor/EditorChrome.h"
#include <cstddef>
#include <span>
#include <string>

namespace demi::editor {

struct EditorPaletteCard {
  std::string id;
  std::string title;
  std::string description;
  EditorIcon icon = EditorIcon::File;
};

// Payload bytes and their type are supplied by the destination's adapter.
// Empty payload makes a click/keyboard-only card. The widget knows no module
// catalog, terrain registry, project, or authored data format.
[[nodiscard]] bool
drawEditorPaletteCard(const EditorPaletteCard &card,
                      const char *payloadType = nullptr,
                      std::span<const std::byte> payload = {});

} // namespace demi::editor
