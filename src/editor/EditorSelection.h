#pragma once

#include <string>

namespace demi::editor {

// A painted isometric cell is an editor selection projected from compact grid
// data. It is not a runtime entity and is never serialized as one.
struct EditorIsoGridCell {
  std::string gridEntityId;
  int x = 0;
  int y = 0;

  friend bool operator==(const EditorIsoGridCell &,
                         const EditorIsoGridCell &) = default;
};

} // namespace demi::editor
