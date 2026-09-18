#pragma once
#include <nlohmann/json_fwd.hpp>

namespace demi::editor {
struct StructuredValueEdit {
  bool changed = false;
  bool continuous = false;
  bool finished = false;
};
// Edits a local candidate only. The owning document performs validation,
// persistence and undo. vectorWidth is 2/3 for reflected vector arrays.
[[nodiscard]] StructuredValueEdit drawStructuredValue(nlohmann::json &value,
                                                      int vectorWidth = 0,
                                                      bool readOnly = false);
} // namespace demi::editor
