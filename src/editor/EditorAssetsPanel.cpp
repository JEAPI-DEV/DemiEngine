#include "editor/EditorAssetsPanel.h"

#include "editor/EditorChrome.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include "demi/filesystem/ProjectPaths.h"

#include <SDL3/SDL.h>
#include <imgui.h>

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
  // Keep file aliases in their authored folder, without filesystem queries
  // during drawing. Directory discovery uses the same lexical paths.
  const auto relative = source.lexically_relative(
      workspace.project().project.projectDirectory);
  return relative.empty() ? source.filename() : relative;
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
                    std::filesystem::path &selected, bool reveal) {
  for (const std::filesystem::path &directory :
       childDirectories(directories, parent)) {
    const bool hasChildren = !childDirectories(directories, directory).empty();
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!hasChildren)
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected == directory)
      flags |= ImGuiTreeNodeFlags_Selected;
    if (reveal && hasChildren) {
      const auto [end, ignored] = std::mismatch(
          directory.begin(), directory.end(), selected.begin(), selected.end());
      if (end == directory.end())
        ImGui::SetNextItemOpen(true);
    }
    const bool open =
        ImGui::TreeNodeEx(directory.generic_string().c_str(), flags, "%s",
                          directory.filename().string().c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      selected = directory;
    if (hasChildren && open) {
      drawFolderTree(directories, directory, selected, reveal);
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
  const bool pressed = ImGui::Selectable("##tile", selected, ImGuiSelectableFlags_AllowDoubleClick, size);
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

void locateSource(const std::filesystem::path &source, std::string &notice) {
  const std::string uri = "file://" + source.parent_path().string();
  if (!SDL_OpenURL(uri.c_str()))
    notice = std::string("Could not open file location: ") + SDL_GetError();
  else
    notice = "Opened " + source.parent_path().string();
}

void drawAssetDetails(EditorWorkspace &workspace,
                      const std::filesystem::path &selected,
                      EditorAssetDialogs &dialogs, std::string &notice) {
  ImGui::TextDisabled("DETAILS");
  if (selected.empty()) {
    ImGui::TextWrapped("Select an authored source or asset manifest.");
    return;
  }
  const std::filesystem::path relative = relativeSource(workspace, selected);
  ImGui::TextWrapped("%s", relative.generic_string().c_str());
  if (ImGui::SmallButton("Locate"))
    locateSource(selected, notice);

  const EditorAssetRecord *record =
      workspace.assetIndex().findByManifest(selected);
  if (record == nullptr)
    return;
  ImGui::SameLine();
  if (ImGui::SmallButton("Reimport")) {
    std::string error;
    notice =
        workspace.reimportAsset(selected, error) ? "Asset reimported" : error;
    return;
  }
  if (record->manifest.type == "Model3D") {
    ImGui::SameLine();
    if (ImGui::SmallButton("Generate collider"))
      dialogs.openGenerateCollider(record->manifest.manifestPath,
                                   record->manifest.id);
  }
  ImGui::Separator();
  ImGui::TextDisabled("Stable ID");
  ImGui::TextWrapped("%s", record->manifest.id.c_str());
  ImGui::TextDisabled("Type");
  ImGui::SameLine(85.0F);
  ImGui::TextUnformatted(record->manifest.type.c_str());
  ImGui::TextDisabled("Importer");
  ImGui::SameLine(85.0F);
  ImGui::Text("%s v%d", record->manifest.importer.c_str(),
              record->manifest.importerVersion);
  ImGui::TextDisabled("Cook");
  ImGui::SameLine(85.0F);
  const ImVec4 cookColor =
      record->cookState == EditorAssetCookState::Current
          ? ImVec4{0.35F, 0.85F, 0.55F, 1.0F}
      : record->cookState == EditorAssetCookState::Stale
          ? ImVec4{0.95F, 0.72F, 0.30F, 1.0F}
          : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
  ImGui::TextColored(cookColor, "%s",
                     editorAssetCookStateName(record->cookState));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", record->cookReason.c_str());

  std::vector<std::string> preloads =
      workspace.projectDocument().preloadedAssets();
  const bool isPreloaded =
      std::ranges::find(preloads, record->manifest.id) != preloads.end();
  if (ImGui::Button(isPreloaded ? "Remove preload" : "Add to preload",
                    {-1.0F, 0.0F})) {
    if (isPreloaded)
      std::erase(preloads, record->manifest.id);
    else
      preloads.push_back(record->manifest.id);
    std::string error;
    notice = workspace.setPreloadedAssets(std::move(preloads), error)
                 ? "Project preload list modified"
                 : error;
  }
  if (!record->manifest.dependencies.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("Dependencies");
    for (const std::string &dependency : record->manifest.dependencies)
      ImGui::BulletText("%s", dependency.c_str());
  }
  for (const Diagnostic &diagnostic : record->diagnostics) {
    const ImVec4 color = diagnostic.severity == Severity::Error
                             ? ImVec4{0.95F, 0.34F, 0.38F, 1.0F}
                             : ImVec4{0.95F, 0.72F, 0.30F, 1.0F};
    ImGui::TextColored(color, "%s", diagnostic.code.c_str());
    ImGui::TextWrapped("%s", diagnostic.message.c_str());
  }
}

} // namespace

void EditorAssetsPanel::draw(EditorWorkspace &workspace, const ImVec2 position,
                             const ImVec2 size, std::string &notice,
                             bool *open) {
  if (!beginEditorPanel("Assets", position, size, open)) {
    ImGui::End();
    return;
  }
  const float panelWidth = ImGui::GetContentRegionAvail().x;
  if (ImGui::Button("+ Import", {76.0F, 28.0F}))
    dialogs_.openImport();
  ImGui::SameLine();
  if (ImGui::Button("+ Create", {76.0F, 28.0F}))
    ImGui::OpenPopup("asset-create-menu");
  if (ImGui::BeginPopup("asset-create-menu")) {
    for (const auto &[name,kind]:std::vector<std::pair<const char*,EditorSourceKind>>{
      {"3D Scene...",EditorSourceKind::Scene3D},{"2D Scene...",EditorSourceKind::Scene2D},
      {"HUD...",EditorSourceKind::Hud},{"Scene Prefab...",EditorSourceKind::Prefab},
      {"UI Prefab...",EditorSourceKind::UiPrefab},{"Lua Script...",EditorSourceKind::Lua},
      {"Material...",EditorSourceKind::Material},{"Data Asset...",EditorSourceKind::Data}})
      if (ImGui::MenuItem(name)) dialogs_.openNewSource(kind);
    ImGui::Separator();
    if (ImGui::MenuItem("New Folder..."))
      dialogs_.openNewFolder(directory_);
    if (ImGui::MenuItem("Asset group..."))
      dialogs_.openCreateGroup();
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Assets%s%s", directory_.empty() ? "" : " / ",
                      directory_.generic_string().c_str());
  ImGui::SameLine(std::max(300.0F, panelWidth - 430.0F));
  ImGui::SetNextItemWidth(135.0F);
  if (ImGui::BeginCombo("##asset-type-filter", typeFilter_.empty()
                                                   ? "All types"
                                                   : typeFilter_.c_str())) {
    if (ImGui::Selectable("All types", typeFilter_.empty()))
      typeFilter_.clear();
    for (const std::string &type : workspace.assetIndex().types())
      if (ImGui::Selectable(type.c_str(), typeFilter_ == type))
        typeFilter_ = type;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220.0F);
  ImGui::InputTextWithHint("##asset-search", "Search assets", filter_.data(),
                           filter_.size());
  ImGui::Separator();

  const auto &directories = workspace.sourceDirectories();
  if (!directory_.empty() && !directories.contains(directory_))
    directory_.clear();
  constexpr float TreeWidth = 145.0F;
  constexpr float DetailsWidth = 245.0F;
  ImGui::BeginChild("asset-tree", {TreeWidth, 0.0F}, ImGuiChildFlags_Borders);
  ImGui::TextDisabled("Favorites");
  ImGui::Spacing();
  if (ImGui::Selectable("Assets", directory_.empty()))
    directory_.clear();
  drawFolderTree(directories, {}, directory_, revealDirectory_);
  revealDirectory_ = false;
  ImGui::Spacing();
  ImGui::TextDisabled("Packages");
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("asset-grid", {-DetailsWidth, 0.0F},
                    ImGuiChildFlags_Borders);
  const float available = ImGui::GetContentRegionAvail().x;
  const int columns = std::max(1, static_cast<int>(available / 112.0F));
  bool hasEntries = false;
  if (ImGui::BeginTable("asset-tiles", columns)) {
    if (filter_[0] == '\0' && typeFilter_.empty()) {
      for (const std::filesystem::path &child :
           childDirectories(directories, directory_)) {
        hasEntries = true;
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
      const EditorAssetRecord *record =
          workspace.assetIndex().findByManifest(source);
      if (!typeFilter_.empty() &&
          (record == nullptr || record->manifest.type != typeFilter_))
        continue;
      if (!containsCaseInsensitive(display, filter_.data()))
        continue;
      if (filter_[0] == '\0' && relative.parent_path() != directory_)
        continue;
      hasEntries = true;
      ImGui::TableNextColumn();
      if (drawAssetTile(display.c_str(), relative.filename().string(),
                        EditorIcon::File, selectedSource_ == source)) {
        selectedSource_ = source;
        notice = "Selected project source: " + display;
        if (isSceneFile(source) || isHudFile(source))
          openRequest_ = source;
      }
      if (ImGui::IsItemHovered() &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        openRequest_ = source;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", display.c_str());
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Open"))
          openRequest_ = source;
        if (ImGui::MenuItem("Locate in filesystem"))
          locateSource(source, notice);
        if (record != nullptr && ImGui::MenuItem("Reimport")) {
          std::string error;
          notice = workspace.reimportAsset(source, error) ? "Asset reimported"
                                                          : error;
        }
        if (record != nullptr && record->manifest.type == "Model3D" &&
            ImGui::MenuItem("Generate Collider Asset..."))
          dialogs_.openGenerateCollider(record->manifest.manifestPath,
                                        record->manifest.id);
        ImGui::EndPopup();
      }
    }
    ImGui::EndTable();
  }
  if (!hasEntries)
    ImGui::TextDisabled(filter_[0] == '\0' && typeFilter_.empty()
                            ? "This folder is empty."
                            : "No matching files.");
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("asset-details", {0.0F, 0.0F}, ImGuiChildFlags_Borders);
  drawAssetDetails(workspace, selectedSource_, dialogs_, notice);
  const auto selectedGroup =
      std::ranges::find(workspace.assetIndex().groups(), selectedSource_,
                        &assets::AssetGroupDescriptor::sourcePath);
  if (selectedGroup != workspace.assetIndex().groups().end()) {
    ImGui::Separator();
    ImGui::TextDisabled("Asset group");
    ImGui::TextWrapped("%s", selectedGroup->id.c_str());
    for (const std::string &root : selectedGroup->roots)
      ImGui::BulletText("%s", root.c_str());
    if (ImGui::Button("Edit roots", {-1.0F, 0.0F})) {
      std::string error;
      if (!dialogs_.openEditGroup(selectedGroup->sourcePath, error))
        notice = error;
    }
  }
  ImGui::EndChild();
  ImGui::End();
  dialogs_.draw(workspace, notice);
  if (auto source=dialogs_.takeCreatedSource()) { selectedSource_=*source;openRequest_=*source; }
  if (auto collider = dialogs_.takeCreatedCollider()) {
    selectedSource_ = *collider;
    directory_ = relativeSource(workspace, *collider).parent_path();
    filter_.fill('\0');
    typeFilter_.clear();
    revealDirectory_ = true;
  }
  if (auto created = dialogs_.takeCreatedFolder()) {
    directory_ = std::move(*created);
    selectedSource_.clear();
    filter_.fill('\0');
    typeFilter_.clear();
    revealDirectory_ = true;
  }
}

} // namespace demi::editor
