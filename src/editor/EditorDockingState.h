#pragma once

#include <filesystem>
#include <string>

namespace demi::editor {

struct EditorPanelVisibility {
  bool hierarchy = true;
  bool stage = true;
  bool inspector = true;
  bool console = true;
  bool assets = true;
  auto operator<=>(const EditorPanelVisibility &) const = default;
};

struct EditorLayoutPreparation {
  bool hasSavedLayout = false;
  bool recoveredCorruptLayout = false;
  std::string diagnostic;
};

class EditorDockingStateStore {
public:
  explicit EditorDockingStateStore(std::filesystem::path editorDataRoot);

  [[nodiscard]] const std::filesystem::path &layoutPath() const noexcept;
  [[nodiscard]] const std::filesystem::path &visibilityPath() const noexcept;
  [[nodiscard]] EditorLayoutPreparation prepareLayout() const;
  [[nodiscard]] bool loadVisibility(EditorPanelVisibility &visibility,
                                    std::string &error) const;
  [[nodiscard]] bool saveVisibility(const EditorPanelVisibility &visibility,
                                    std::string &error) const;

private:
  std::filesystem::path layoutPath_;
  std::filesystem::path visibilityPath_;
};

} // namespace demi::editor
