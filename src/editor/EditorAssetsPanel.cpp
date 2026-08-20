#include "editor/EditorAssetsPanel.h"

#include "editor/EditorChrome.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>
#include <vector>

namespace demi::editor {
namespace {

bool containsCaseInsensitive(const std::string_view value,
                             const std::string_view filter) {
  if (filter.empty())
    return true;
  std::string haystack(value);
  std::string needle(filter);
  const auto lower = [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  };
  std::ranges::transform(haystack, haystack.begin(), lower);
  std::ranges::transform(needle, needle.begin(), lower);
  return haystack.find(needle) != std::string::npos;
}

std::filesystem::path relativeSource(const EditorWorkspace &workspace,
                                     const std::filesystem::path &source) {
  std::error_code error;
  const std::filesystem::path relative = std::filesystem::relative(
      source, workspace.project().project.projectDirectory, error);
  return error ? source.filename() : relative;
}

std::set<std::filesystem::path>
allDirectories(const EditorWorkspace &workspace) {
  std::set<std::filesystem::path> directories;
  for (const std::filesystem::path &source : workspace.sources()) {
    std::filesystem::path parent =
        relativeSource(workspace, source).parent_path();
    while (!parent.empty() && parent != ".") {
      directories.insert(parent);
      parent = parent.parent_path();
    }
  }
  return directories;
}

std::vector<std::filesystem::path>
childDirectories(const std::set<std::filesystem::path> &directories,
                 const std::filesystem::path &parent) {
  std::vector<std::filesystem::path> children;
  for (const std::filesystem::path &directory : directories)
    if (directory.parent_path() == parent)
      children.push_back(directory);
  return children;
}

void drawFolderTree(const std::set<std::filesystem::path> &directories,
                    const std::filesystem::path &parent,
                    std::filesystem::path &selected) {
  for (const std::filesystem::path &directory :
       childDirectories(directories, parent)) {
    const bool hasChildren = !childDirectories(directories, directory).empty();
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!hasChildren)
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected == directory)
      flags |= ImGuiTreeNodeFlags_Selected;
    const bool open =
        ImGui::TreeNodeEx(directory.generic_string().c_str(), flags, "%s",
                          directory.filename().string().c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      selected = directory;
    if (hasChildren && open) {
      drawFolderTree(directories, directory, selected);
      ImGui::TreePop();
    }
  }
}

bool drawAssetTile(const char *id, const std::string &label,
                   const EditorIcon icon, const bool selected,
                   const ImVec2 size = {102.0F, 86.0F}) {
  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Header, selected
                                             ? ImVec4{0.27F, 0.21F, 0.40F, 1.0F}
                                             : ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.18F, 0.18F, 0.22F, 1.0F});
  const bool pressed = ImGui::Selectable("##tile", selected, 0, size);
  ImGui::PopStyleColor(2);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  draw->AddRect(min, max, IM_COL32(52, 55, 63, 255), 2.0F);
  drawEditorGlyph(*draw, icon, {min.x + size.x * 0.5F, min.y + 31.0F},
                  icon == EditorIcon::Folder ? IM_COL32(178, 181, 187, 255)
                                             : IM_COL32(150, 153, 162, 255),
                  1.65F);
  std::string renderedLabel = label;
  bool truncated = false;
  while (renderedLabel.size() > 3 &&
         ImGui::CalcTextSize((renderedLabel + "...").c_str()).x >
             size.x - 10.0F) {
    renderedLabel.pop_back();
    truncated = true;
  }
  if (truncated)
    renderedLabel += "...";
  const ImVec2 textSize = ImGui::CalcTextSize(renderedLabel.c_str());
  const float x = std::max(min.x + 4.0F, min.x + (size.x - textSize.x) * 0.5F);
  draw->AddText({x, max.y - 22.0F}, IM_COL32(205, 208, 216, 255),
                renderedLabel.c_str(),
                renderedLabel.c_str() + renderedLabel.size());
  ImGui::PopID();
  return pressed;
}

} // namespace

void EditorAssetsPanel::draw(EditorWorkspace &workspace, const ImVec2 position,
                             const ImVec2 size, std::string &notice) {
  beginEditorPanel("Assets", position, size);
  (void)editorStageTab("Assets", true, {67.0F, 25.0F});
  ImGui::SameLine(0.0F, 2.0F);
  ImGui::BeginDisabled();
  (void)editorStageTab("Lua Console", false, {92.0F, 25.0F});
  ImGui::EndDisabled();
  ImGui::SameLine(size.x - 27.0F);
  ImGui::TextDisabled("x");
  ImGui::Separator();

  ImGui::BeginDisabled();
  ImGui::Button("+  Import", {82.0F, 28.0F});
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Import is connected during Milestone 7.");
  ImGui::SameLine();
  ImGui::TextDisabled("Assets%s%s", directory_.empty() ? "" : " / ",
                      directory_.generic_string().c_str());
  ImGui::SameLine(std::max(290.0F, size.x - 255.0F));
  ImGui::SetNextItemWidth(220.0F);
  ImGui::InputTextWithHint("##asset-search", "Search assets", filter_.data(),
                           filter_.size());
  ImGui::Separator();

  const std::set<std::filesystem::path> directories = allDirectories(workspace);
  if (!directory_.empty() && !directories.contains(directory_))
    directory_.clear();
  constexpr float TreeWidth = 145.0F;
  ImGui::BeginChild("asset-tree", {TreeWidth, 0.0F}, ImGuiChildFlags_Borders);
  ImGui::TextDisabled("Favorites");
  ImGui::Spacing();
  const bool rootSelected = directory_.empty();
  if (ImGui::Selectable("Assets", rootSelected))
    directory_.clear();
  drawFolderTree(directories, {}, directory_);
  ImGui::Spacing();
  ImGui::TextDisabled("Packages");
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("asset-grid", {0.0F, 0.0F}, ImGuiChildFlags_Borders);
  const float available = ImGui::GetContentRegionAvail().x;
  const int columns = std::max(1, static_cast<int>(available / 112.0F));
  if (ImGui::BeginTable("asset-tiles", columns)) {
    if (filter_[0] == '\0') {
      for (const std::filesystem::path &child :
           childDirectories(directories, directory_)) {
        ImGui::TableNextColumn();
        const std::string id = child.generic_string();
        if (drawAssetTile(id.c_str(), child.filename().string(),
                          EditorIcon::Folder, false))
          directory_ = child;
      }
    }
    for (const std::filesystem::path &source : workspace.sources()) {
      const std::filesystem::path relative = relativeSource(workspace, source);
      const std::string display = relative.generic_string();
      if (!containsCaseInsensitive(display, filter_.data()))
        continue;
      if (filter_[0] == '\0' && relative.parent_path() != directory_)
        continue;
      ImGui::TableNextColumn();
      if (drawAssetTile(display.c_str(), relative.filename().string(),
                        EditorIcon::File, selectedSource_ == source)) {
        selectedSource_ = source;
        notice = "Selected project source: " + display;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", display.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
  ImGui::End();
}

} // namespace demi::editor
