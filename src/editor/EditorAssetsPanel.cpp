#include "editor/EditorAssetsPanel.h"

#include "editor/EditorChrome.h"
#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include "demi/filesystem/ProjectPaths.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

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
  const auto relative =
      source.lexically_relative(workspace.project().project.projectDirectory);
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

std::optional<std::filesystem::path>
prefabSourceDirectory(const std::filesystem::path &directory) {
  const std::filesystem::path normalized = directory.lexically_normal();
  if (normalized.empty() || normalized == ".")
    return std::filesystem::path("prefabs");
  const auto relative = normalized.lexically_relative("prefabs");
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    return std::nullopt;
  return normalized;
}

std::string suggestedPrefabName(const nlohmann::json &entity,
                                const std::string_view entityId) {
  std::string suggestion = entity.value("name", std::string(entityId));
  for (char &character : suggestion) {
    const auto value = static_cast<unsigned char>(character);
    if (!std::isalnum(value) && character != '_' && character != '-')
      character = '_';
  }
  while (!suggestion.empty() && suggestion.front() == '_')
    suggestion.erase(suggestion.begin());
  while (!suggestion.empty() && suggestion.back() == '_')
    suggestion.pop_back();
  return suggestion.empty() ? "prefab" : suggestion;
}

void acceptHierarchyEntityDrop(EditorWorkspace &workspace,
                               EditorAssetDialogs &dialogs,
                               const std::filesystem::path &directory,
                               std::string &notice) {
  if (!ImGui::BeginDragDropTarget())
    return;
  if (const ImGuiPayload *payload =
          ImGui::AcceptDragDropPayload(EditorSceneEntityPayload);
      payload != nullptr && payload->Data != nullptr && payload->DataSize > 1) {
    const auto destination = prefabSourceDirectory(directory);
    const auto *data = static_cast<const char *>(payload->Data);
    const bool terminated = data[payload->DataSize - 1] == '\0';
    const std::string entityId =
        terminated
            ? std::string(data,
                          static_cast<std::size_t>(payload->DataSize - 1))
            : std::string{};
    const nlohmann::json *entity =
        terminated && !entityId.empty()
            ? workspace.sceneDocument().entity(entityId)
            : nullptr;
    if (!destination) {
      notice = "Entity prefabs must be created in the prefabs folder or one "
               "of its subfolders.";
    } else if (entity == nullptr) {
      notice = "Only authored hierarchy entities can become prefabs; select "
               "an authored instance root instead of expanded preview data.";
    } else {
      dialogs.openNewSource(EditorSourceKind::PrefabFromSelection, entityId,
                            *destination,
                            suggestedPrefabName(*entity, entityId));
    }
  }
  ImGui::EndDragDropTarget();
}

void drawFolderTree(const std::set<std::filesystem::path> &directories,
                    const std::filesystem::path &parent,
                    std::filesystem::path &selected, bool reveal,
                    EditorWorkspace &workspace, EditorAssetDialogs &dialogs,
                    std::string &notice) {
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
    acceptHierarchyEntityDrop(workspace, dialogs, directory, notice);
    if (hasChildren && open) {
      drawFolderTree(directories, directory, selected, reveal, workspace,
                     dialogs, notice);
      ImGui::TreePop();
    }
  }
}

EditorIcon sourceIcon(const std::filesystem::path &source) {
  if (isPrefabFile(source))
    return EditorIcon::Prefab;
  if (isSceneFile(source))
    return EditorIcon::Scene;
  if (isHudFile(source) || isUiPrefabFile(source))
    return EditorIcon::Hud;
  return EditorIcon::File;
}

bool drawAssetTile(const char *id, const std::string &label,
                   const EditorIcon icon, const bool selected,
                   const ImVec2 size = {102.0F, 86.0F}) {
  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Header, selected
                                             ? ImVec4{0.27F, 0.21F, 0.40F, 1.0F}
                                             : ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.18F, 0.18F, 0.22F, 1.0F});
  const bool pressed = ImGui::Selectable(
      "##tile", selected, ImGuiSelectableFlags_AllowDoubleClick, size);
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

  if (isPrefabFile(selected)) {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Reusable entity hierarchy. Double-click to edit its source.");
    ImGui::BeginDisabled(workspace.activeDocument() !=
                         EditorWorkspaceDocument::Scene);
    if (ImGui::Button("Add prefab to active scene", {-1.0F, 0.0F})) {
      std::string error;
      notice = workspace.instantiatePrefab(selected, error)
                   ? "Prefab instance added"
                   : error;
    }
    ImGui::EndDisabled();
  }

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
  if (ImGui::Button("+ Import"))
    dialogs_.openImport();
  ImGui::SameLine();
  if (ImGui::Button("+ Create"))
    ImGui::OpenPopup("asset-create-menu");
  if (ImGui::BeginPopup("asset-create-menu")) {
    for (const auto &[name, kind] :
         std::vector<std::pair<const char *, EditorSourceKind>>{
             {"3D Scene...", EditorSourceKind::Scene3D},
             {"2D Scene...", EditorSourceKind::Scene2D},
             {"HUD...", EditorSourceKind::Hud},
             {"3D Entity Prefab...", EditorSourceKind::Prefab},
             {"2D Entity Prefab...", EditorSourceKind::Prefab2D},
             {"UI Prefab...", EditorSourceKind::UiPrefab},
             {"Lua Script...", EditorSourceKind::Lua},
             {"Material...", EditorSourceKind::Material},
             {"Data Asset...", EditorSourceKind::Data}})
      if (ImGui::MenuItem(name))
        dialogs_.openNewSource(kind);
    const auto selection = workspace.selectedEntityId();
    if (ImGui::MenuItem("Prefab from selected hierarchy...", nullptr, false,
                        !selection.empty() &&
                            workspace.sceneDocument().entity(selection)))
      dialogs_.openNewSource(EditorSourceKind::PrefabFromSelection,
                             std::string(selection));
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
  ImGui::SetNextItemWidth(
      std::min(panelWidth * 0.35F, ImGui::GetFontSize() * 10.0F));
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
  ImGui::SetNextItemWidth(-1.0F);
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
  acceptHierarchyEntityDrop(workspace, dialogs_, "prefabs", notice);
  drawFolderTree(directories, {}, directory_, revealDirectory_, workspace,
                 dialogs_, notice);
  revealDirectory_ = false;
  ImGui::Spacing();
  ImGui::TextDisabled("Packages");
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("asset-grid", {-DetailsWidth, 0.0F},
                    ImGuiChildFlags_Borders);
  if (const ImGuiPayload *payload = ImGui::GetDragDropPayload();
      payload != nullptr && payload->IsDataType(EditorSceneEntityPayload)) {
    const std::filesystem::path destination =
        prefabSourceDirectory(directory_).value_or("prefabs");
    const std::string label =
        "Create prefab in " + destination.generic_string();
    ImGui::PushStyleColor(ImGuiCol_Header, {0.10F, 0.23F, 0.36F, 1.0F});
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          {0.13F, 0.30F, 0.46F, 1.0F});
    ImGui::Selectable(label.c_str(), false, 0, {-1.0F, 30.0F});
    ImGui::PopStyleColor(2);
    acceptHierarchyEntityDrop(workspace, dialogs_, destination, notice);
    ImGui::Spacing();
  }
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
        acceptHierarchyEntityDrop(workspace, dialogs_, child, notice);
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
                        sourceIcon(source), selectedSource_ == source)) {
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
      if (isPrefabFile(source) && ImGui::BeginDragDropSource()) {
        const std::string path = source.string();
        ImGui::SetDragDropPayload(EditorPrefabSourcePayload, path.c_str(),
                                  path.size() + 1);
        ImGui::TextUnformatted(source.filename().string().c_str());
        ImGui::TextDisabled(
            "Drop onto the scene Viewport or Scene in the Hierarchy");
        ImGui::EndDragDropSource();
      }
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Open"))
          openRequest_ = source;
        if (isPrefabFile(source) &&
            ImGui::MenuItem("Add prefab to active scene", nullptr, false,
                            workspace.activeDocument() ==
                                EditorWorkspaceDocument::Scene)) {
          std::string error;
          notice = workspace.instantiatePrefab(source, error)
                       ? "Prefab instance added"
                       : error;
        }
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
  // The child item is the broad grid fallback. ImGui keeps an overlapping
  // folder tile as the active target because its rectangle is smaller.
  const std::filesystem::path gridDropDirectory =
      prefabSourceDirectory(directory_).value_or("prefabs");
  acceptHierarchyEntityDrop(workspace, dialogs_, gridDropDirectory, notice);
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
  if (auto source = dialogs_.takeCreatedSource()) {
    selectedSource_ = *source;
    directory_ = relativeSource(workspace, *source).parent_path();
    filter_.fill('\0');
    typeFilter_.clear();
    revealDirectory_ = true;
    if (dialogs_.opensCreatedSource())
      openRequest_ = *source;
  }
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
