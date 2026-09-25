#pragma once
#include "demi/runtime/ui/FontRasterizer.h"
#include <memory>
struct ImFontLoader;
namespace demi::editor {
// Bridges ImGui's font-loader interface to the same rasterizer used by game UI.
// Must outlive the ImGui context and all fonts that reference it.
class EditorFontLoader {
public:
  explicit EditorFontLoader(runtime::ui::FontVariations variations);
  ~EditorFontLoader();
  const ImFontLoader *loader() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace demi::editor
