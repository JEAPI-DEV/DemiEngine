#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::editor {
struct EditorInputRoute {
  runtime::InputState input;
  bool changed = false;
  bool exclusive = false;
};
// Filters only the editor's copy. The runtime retains the original snapshot.
class EditorInputOwnership {
public:
  EditorInputRoute route(const runtime::InputState &, bool exclusive);

private:
  bool exclusive_ = false;
  std::unordered_set<std::string> blockedButtons_, blockedKeys_;
};
} // namespace demi::editor
