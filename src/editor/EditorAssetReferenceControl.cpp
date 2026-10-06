#include "editor/EditorAssetReferenceControl.h"

#include <imgui.h>

namespace demi::editor {

bool drawEditorAssetReferenceControl(const char *label,
                                     const AssetRegistry &registry,
                                     const EditorAssetPredicate &accepts,
                                     std::string &selected,
                                     const bool allowNone) {
  bool changed = false;
  const char *preview = selected.empty() ? "None" : selected.c_str();
  if (!ImGui::BeginCombo(label, preview))
    return false;

  if (allowNone && ImGui::Selectable("None", selected.empty())) {
    changed = !selected.empty();
    selected.clear();
  }
  for (const AssetManifest &manifest : registry.assets) {
    if (!accepts(manifest))
      continue;
    if (ImGui::Selectable(manifest.id.c_str(), selected == manifest.id)) {
      changed = selected != manifest.id;
      selected = manifest.id;
    }
  }
  ImGui::EndCombo();
  return changed;
}

} // namespace demi::editor
