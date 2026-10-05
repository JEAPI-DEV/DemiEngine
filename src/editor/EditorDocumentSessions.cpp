#include "editor/EditorDocumentSessions.h"

#include <algorithm>
#include <array>
#include <map>
#include <string_view>

namespace demi::editor {
namespace {

constexpr std::string_view PrefabSessionPrefix = "prefab-session:";
constexpr std::string_view TerrainSessionPrefix = "terrain-session:";
constexpr std::string_view SceneSessionPrefix = "scene-session:";
constexpr std::string_view HudSessionPrefix = "hud-session:";

std::filesystem::path documentIdentity(const std::filesystem::path &path) {
  if (path.empty())
    return {};
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  return error ? std::filesystem::absolute(path).lexically_normal() : canonical;
}

bool hasPendingChanges(const EditorWorkspace &workspace) {
  return workspace.hasUnsavedChanges() ||
         workspace.terrainAuthoring().hasDraftChanges();
}

void appendRecoveryDocuments(std::vector<EditorRecoveryDocument> &output,
                             const EditorWorkspace *workspace,
                             const std::string_view prefix) {
  if (!workspace)
    return;
  for (auto document : workspace->dirtyDocuments()) {
    document.kind.insert(0, prefix);
    output.push_back(std::move(document));
  }
}

bool validateRecoveryDocuments(const EditorRecoverySnapshot &snapshot,
                               std::string &error) {
  std::map<std::filesystem::path, const EditorRecoveryDocument *> seen;
  std::map<std::string, std::size_t> counts;
  for (const auto &document : snapshot.documents) {
    if (document.path.empty()) {
      error = "Recovery contains a document without a source path.";
      return false;
    }
    if (document.kind != "scene" && document.kind != "prefab" &&
        document.kind != "hud" && document.kind != "project" &&
        document.kind != "terrain-asset") {
      error = "Unsupported recovery document kind: " + document.kind;
      return false;
    }
    const auto [found, inserted] =
        seen.emplace(documentIdentity(document.path), &document);
    if (!inserted && (found->second->kind != document.kind ||
                      found->second->content != document.content)) {
      error = "Recovery contains conflicting drafts in one document session: " +
              document.path.string();
      return false;
    }
    if (inserted &&
        ++counts[document.kind] > (document.kind == "hud" ? 2U : 1U)) {
      error = "Recovery contains more documents than this session can own: " +
              document.kind;
      return false;
    }
  }
  return true;
}

bool recoverWorkspace(EditorWorkspace &workspace,
                      EditorRecoverySnapshot snapshot, std::string &error) {
  // A prefab is the entity document in its own session. The workspace's older
  // combined recovery path would reopen the main scene before restoring it.
  for (auto &document : snapshot.documents)
    if (document.kind == "prefab")
      document.kind = "scene";
  return workspace.applyRecovery(snapshot, error);
}

} // namespace

EditorWorkspace *
EditorDocumentSessions::workspace(const EditorDocumentSession session) {
  switch (session) {
  case EditorDocumentSession::Scene:
    return &scene_;
  case EditorDocumentSession::Prefab:
    return prefab_.get();
  case EditorDocumentSession::TerrainAsset:
    return terrain_.get();
  case EditorDocumentSession::Hud:
    return hud_.get();
  }
  return nullptr;
}

const EditorWorkspace *
EditorDocumentSessions::workspace(const EditorDocumentSession session) const {
  switch (session) {
  case EditorDocumentSession::Scene:
    return &scene_;
  case EditorDocumentSession::Prefab:
    return prefab_.get();
  case EditorDocumentSession::TerrainAsset:
    return terrain_.get();
  case EditorDocumentSession::Hud:
    return hud_.get();
  }
  return nullptr;
}

EditorWorkspace &EditorDocumentSessions::focused() {
  auto *active = workspace(focused_);
  return active ? *active : scene_;
}

const EditorWorkspace &EditorDocumentSessions::focused() const {
  const auto *active = workspace(focused_);
  return active ? *active : scene_;
}

bool EditorDocumentSessions::focus(const EditorDocumentSession session,
                                   std::string &error) {
  if (!synchronizeProject(error))
    return false;
  if (session == EditorDocumentSession::Hud && !hud_ && scene_.hudDocument())
    return openHud(scene_.hudDocument()->path(), error);
  if (!workspace(session)) {
    error = "Open a document before focusing its view.";
    return false;
  }
  focused_ = session;
  return true;
}

bool EditorDocumentSessions::canReplace(const EditorWorkspace &workspace,
                                        std::string &error) const {
  if (!hasPendingChanges(workspace))
    return true;
  error = "Save or undo document changes and generate or discard terrain "
          "drafts before replacing this document session.";
  return false;
}

bool EditorDocumentSessions::openPrefab(const std::filesystem::path &path,
                                        std::string &error) {
  if (!synchronizeProject(error))
    return false;
  if (prefab_ && documentIdentity(prefab_->sceneDocument().path()) ==
                     documentIdentity(path))
    return focus(EditorDocumentSession::Prefab, error);
  if (prefab_ && !canReplace(*prefab_, error))
    return false;
  auto candidate = std::make_unique<EditorWorkspace>();
  if (!candidate->openProjectContext(scene_, error) ||
      !candidate->openPrefabDocument(path, error))
    return false;
  prefab_ = std::move(candidate);
  return focus(EditorDocumentSession::Prefab, error);
}

bool EditorDocumentSessions::openTerrainAsset(const std::filesystem::path &path,
                                              std::string &error) {
  if (!synchronizeProject(error))
    return false;
  if (terrain_ && terrain_->terrainAssetDocument()) {
    const auto &document = *terrain_->terrainAssetDocument();
    if (documentIdentity(document.path()) == documentIdentity(path) ||
        documentIdentity(document.manifestPath()) == documentIdentity(path))
      return focus(EditorDocumentSession::TerrainAsset, error);
  }
  if (terrain_ && !canReplace(*terrain_, error))
    return false;
  auto candidate = std::make_unique<EditorWorkspace>();
  if (!candidate->openProjectContext(scene_, error) ||
      !candidate->openTerrainAssetDocument(path, error))
    return false;
  terrain_ = std::move(candidate);
  return focus(EditorDocumentSession::TerrainAsset, error);
}

bool EditorDocumentSessions::openHud(const std::filesystem::path &path,
                                     std::string &error) {
  if (!synchronizeProject(error))
    return false;
  if (hud_ && hud_->hudDocument() &&
      documentIdentity(hud_->hudDocument()->path()) == documentIdentity(path))
    return focus(EditorDocumentSession::Hud, error);
  if (hud_ && !canReplace(*hud_, error))
    return false;
  auto candidate = std::make_unique<EditorWorkspace>();
  if (!candidate->openProjectContext(scene_, error) ||
      !candidate->openHudDocument(path, error))
    return false;
  hud_ = std::move(candidate);
  return focus(EditorDocumentSession::Hud, error);
}

bool EditorDocumentSessions::saveAll(std::string &error) {
  if (!synchronizeProject(error))
    return false;
  const std::array workspaces{&scene_, prefab_.get(), terrain_.get(),
                              hud_.get()};
  std::map<std::filesystem::path, const EditorWorkspace *> dirtyOwners;
  std::vector<std::filesystem::path> changedHudSources;
  bool projectWasDirty = false;
  // Preflight all workspaces before the first write. Never publish one draft
  // then discover another unsaved session owns the same source.
  for (const auto *workspace : workspaces) {
    if (!workspace)
      continue;
    projectWasDirty |= workspace->projectDocument().isDirty();
    if (!workspace->terrainReady(error))
      return false;
    if (workspace->terrainAuthoring().hasDraftChanges()) {
      error = "Generate or discard terrain graph drafts before saving all "
              "document sessions.";
      return false;
    }
    for (const auto &document : workspace->dirtyDocuments()) {
      if (document.kind == "hud")
        changedHudSources.push_back(document.path);
      const auto [found, inserted] =
          dirtyOwners.emplace(documentIdentity(document.path), workspace);
      if (!inserted && found->second != workspace) {
        error = "The same source has unsaved edits in multiple document "
                "sessions. Resolve those drafts before saving all: " +
                document.path.string();
        return false;
      }
    }
  }
  for (auto *workspace : workspaces)
    if (workspace && !workspace->saveAll(error))
      return false;
  for (const auto &path : changedHudSources)
    if (!refreshHudReferences(path, error))
      return false;
  if (projectWasDirty && !refreshProjectReferences(error))
    return false;
  return true;
}

bool EditorDocumentSessions::refreshHudReferences(
    const std::filesystem::path &path, std::string &error) {
  if (!synchronizeProject(error))
    return false;
  for (auto *workspace : {&scene_, prefab_.get(), terrain_.get(), hud_.get()})
    if (workspace && !workspace->refreshCleanHudDocument(path, error))
      return false;
  return true;
}

bool EditorDocumentSessions::hasUnsavedChanges() const {
  return hasPendingChanges(scene_) ||
         (prefab_ && hasPendingChanges(*prefab_)) ||
         (terrain_ && hasPendingChanges(*terrain_)) ||
         (hud_ && hasPendingChanges(*hud_));
}

std::vector<EditorRecoveryDocument>
EditorDocumentSessions::dirtyDocuments() const {
  std::vector<EditorRecoveryDocument> documents;
  appendRecoveryDocuments(documents, &scene_, SceneSessionPrefix);
  appendRecoveryDocuments(documents, prefab_.get(), PrefabSessionPrefix);
  appendRecoveryDocuments(documents, terrain_.get(), TerrainSessionPrefix);
  appendRecoveryDocuments(documents, hud_.get(), HudSessionPrefix);
  return documents;
}

bool EditorDocumentSessions::applyRecovery(
    const EditorRecoverySnapshot &snapshot, std::string &error) {
  if (!synchronizeProject(error))
    return false;
  if (documentIdentity(snapshot.projectPath) !=
      documentIdentity(scene_.projectPath())) {
    error = "Recovery belongs to another project.";
    return false;
  }
  if (hasUnsavedChanges()) {
    error = "Save or undo current document changes and discard terrain drafts "
            "before restoring a recovered session.";
    return false;
  }
  std::array<EditorRecoverySnapshot, 4> grouped;
  for (auto &group : grouped)
    group.projectPath = snapshot.projectPath;
  for (auto document : snapshot.documents) {
    std::size_t session = 0;
    if (document.kind.starts_with(SceneSessionPrefix)) {
      document.kind.erase(0, SceneSessionPrefix.size());
    } else if (document.kind.starts_with(PrefabSessionPrefix)) {
      session = 1;
      document.kind.erase(0, PrefabSessionPrefix.size());
    } else if (document.kind.starts_with(TerrainSessionPrefix)) {
      session = 2;
      document.kind.erase(0, TerrainSessionPrefix.size());
    } else if (document.kind.starts_with(HudSessionPrefix)) {
      session = 3;
      document.kind.erase(0, HudSessionPrefix.size());
    } else if (document.kind == "prefab") {
      session = 1;
    }
    grouped[session].documents.push_back(std::move(document));
  }
  for (const auto &group : grouped)
    if (!validateRecoveryDocuments(group, error))
      return false;

  std::array<std::unique_ptr<EditorWorkspace>, 4> candidates;
  for (std::size_t session = 0; session < grouped.size(); ++session) {
    const auto &group = grouped[session];
    if (group.documents.empty())
      continue;
    auto candidate = std::make_unique<EditorWorkspace>();
    if (!candidate->openProjectContext(scene_, error))
      return false;
    if (session == 0) {
      const auto scene = std::ranges::find(group.documents, "scene",
                                           &EditorRecoveryDocument::kind);
      const auto path = scene == group.documents.end()
                            ? scene_.sceneDocument().path()
                            : scene->path;
      if (!path.empty() && !candidate->openSceneDocument(path, error))
        return false;
      const auto terrain = std::ranges::find(group.documents, "terrain-asset",
                                             &EditorRecoveryDocument::kind);
      if (terrain != group.documents.end()) {
        if (!candidate->openTerrainAssetDocument(terrain->path, error) ||
            !candidate->activateSceneDocument(error))
          return false;
      }
    } else if (session == 1) {
      const auto prefab = std::ranges::find(group.documents, "prefab",
                                            &EditorRecoveryDocument::kind);
      const auto path = prefab != group.documents.end() ? prefab->path
                        : prefab_ ? prefab_->sceneDocument().path()
                                  : std::filesystem::path{};
      if (path.empty() || !candidate->openPrefabDocument(path, error)) {
        if (path.empty())
          error = "Recovery is missing the prefab session's source document.";
        return false;
      }
    } else if (session == 2) {
      const auto terrain = std::ranges::find(group.documents, "terrain-asset",
                                             &EditorRecoveryDocument::kind);
      const auto path = terrain != group.documents.end() ? terrain->path
                        : terrain_ ? terrain_->lastTerrainAssetPath()
                                   : std::filesystem::path{};
      if (path.empty() || !candidate->openTerrainAssetDocument(path, error)) {
        if (path.empty())
          error = "Recovery is missing the terrain session's source document.";
        return false;
      }
    } else {
      const auto hud = std::ranges::find(group.documents, "hud",
                                         &EditorRecoveryDocument::kind);
      const auto path = hud != group.documents.end() ? hud->path
                        : hud_ && hud_->hudDocument()
                            ? hud_->hudDocument()->path()
                            : std::filesystem::path{};
      if (path.empty() || !candidate->openHudDocument(path, error)) {
        if (path.empty())
          error = "Recovery is missing the HUD session's source document.";
        return false;
      }
    }
    if (!recoverWorkspace(*candidate, group, error))
      return false;
    candidates[session] = std::move(candidate);
  }
  // Publish only when every recovered projection has loaded and validated.
  // Failed recovery cannot replace some views while retaining others.
  if (candidates[0])
    scene_ = std::move(*candidates[0]);
  if (candidates[1])
    prefab_ = std::move(candidates[1]);
  if (candidates[2])
    terrain_ = std::move(candidates[2]);
  if (candidates[3])
    hud_ = std::move(candidates[3]);
  return true;
}

bool EditorDocumentSessions::pollTerrain(std::string &error) {
  if (!synchronizeProject(error))
    return false;
  bool succeeded = true;
  for (auto *workspace : {&scene_, prefab_.get(), terrain_.get(), hud_.get()}) {
    std::string issue;
    if (workspace && !workspace->pollTerrainAuthoring(issue)) {
      if (error.empty())
        error = std::move(issue);
      succeeded = false;
    }
  }
  return succeeded;
}

bool EditorDocumentSessions::synchronizeProject(std::string &error) {
  if (projectPath_ == scene_.projectPath())
    return true;
  if ((prefab_ && hasPendingChanges(*prefab_)) ||
      (terrain_ && hasPendingChanges(*terrain_)) ||
      (hud_ && hasPendingChanges(*hud_))) {
    error = "Another project was opened while secondary document sessions "
            "still have unsaved changes. Reopen their project and save or "
            "discard those changes before switching.";
    return false;
  }
  prefab_.reset();
  terrain_.reset();
  hud_.reset();
  focused_ = EditorDocumentSession::Scene;
  projectPath_ = scene_.projectPath();
  return true;
}

bool EditorDocumentSessions::refreshProjectReferences(std::string &error) {
  if (!synchronizeProject(error))
    return false;
  const std::array workspaces{&scene_, prefab_.get(), terrain_.get(),
                              hud_.get()};
  for (const auto *workspace : workspaces) {
    if (workspace && workspace->projectDocument().isDirty()) {
      error = "Project settings have unsaved edits in a document session. "
              "Save or undo them before adopting saved project metadata.";
      return false;
    }
  }
  for (auto *workspace : workspaces)
    if (workspace &&
        !workspace->refreshCleanProjectDocument(scene_.projectPath(), error))
      return false;
  return true;
}

} // namespace demi::editor
