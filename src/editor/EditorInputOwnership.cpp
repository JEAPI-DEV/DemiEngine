#include "editor/EditorInputOwnership.h"
#include <algorithm>
namespace demi::editor {
EditorInputRoute EditorInputOwnership::route(const runtime::InputState &raw,
                                             bool exclusive) {
  EditorInputRoute result{.changed = exclusive != exclusive_,
                          .exclusive = exclusive};
  if (result.changed) {
    blockedButtons_ = raw.mouseButtonsDown;
    blockedKeys_ = raw.keysDown;
  }
  exclusive_ = exclusive;
  std::erase_if(blockedButtons_, [&](const std::string &key) {
    return !raw.mouseButtonsDown.contains(key);
  });
  std::erase_if(blockedKeys_, [&](const std::string &key) {
    return !raw.keysDown.contains(key);
  });
  if (exclusive) {
    // Ctrl+D remains an editor escape hatch; gameplay cannot suppress it.
    const bool ctrl = raw.keysDown.contains("left ctrl") ||
                      raw.keysDown.contains("right ctrl");
    for (const std::string key : {"left ctrl", "right ctrl", "d"}) {
      if (key != "d" || ctrl) {
        if (raw.keysDown.contains(key))
          result.input.keysDown.insert(key);
        if (raw.keysPressed.contains(key))
          result.input.keysPressed.insert(key);
      }
      if (raw.keysReleased.contains(key) || (key == "d" && !ctrl))
        result.input.keysReleased.insert(key);
    }
    return result;
  }
  result.input = raw;
  if (!blockedKeys_.empty()) {
    result.input.textEntered.clear();
    result.input.textComposition.clear();
    result.input.textCompositionChanged = false;
  }
  for (const auto &key : blockedButtons_) {
    result.input.mouseButtonsDown.erase(key);
    result.input.mouseButtonsPressed.erase(key);
    result.input.mouseButtonsReleased.erase(key);
  }
  for (const auto &key : blockedKeys_) {
    result.input.keysDown.erase(key);
    result.input.keysPressed.erase(key);
    result.input.keysReleased.erase(key);
  }
  return result;
}
} // namespace demi::editor
