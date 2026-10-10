#pragma once

#include "editor/EditorWorkspace.h"

#include <memory>

namespace demi::editor {
enum class EditorDocumentSession { Scene, Prefab, TerrainAsset, Hud };

// Separate authored projections and histories reuse the same workspace model.
// The scene remains available while a prefab or terrain asset is edited.
class EditorDocumentSessions {
public:
  explicit EditorDocumentSessions(EditorWorkspace &scene)
      : scene_(scene), projectPath_(scene.projectPath()) {}
  EditorWorkspace &scene() { return scene_; }
  const EditorWorkspace &scene() const { return scene_; }
  EditorWorkspace &focused();
  const EditorWorkspace &focused() const;
  EditorWorkspace *workspace(EditorDocumentSession session);
  const EditorWorkspace *workspace(EditorDocumentSession session) const;
  bool focus(EditorDocumentSession session, std::string &error);
  bool openPrefab(const std::filesystem::path &path, std::string &error);
  bool openTerrainAsset(const std::filesystem::path &path, std::string &error);
  bool openHud(const std::filesystem::path &path, std::string &error);
  bool saveAll(std::string &error);
  // Use after an individual HUD Save as well as the manager's Save All path.
  bool refreshHudReferences(const std::filesystem::path &path,
                            std::string &error);
  bool refreshPrefabReferences(std::string &error);
  bool hasUnsavedChanges() const;
  std::vector<EditorRecoveryDocument> dirtyDocuments() const;
  bool applyRecovery(const EditorRecoverySnapshot &snapshot,
                     std::string &error);
  bool pollTerrain(std::string &error);
  // Call after a project switch, before presenting secondary views. Dirty old
  // sessions are retained and reported rather than silently closed.
  bool synchronizeProject(std::string &error);
  // Call after a project save or registered-source creation in any session.
  bool refreshProjectReferences(std::string &error);

private:
  bool canReplace(const EditorWorkspace &workspace, std::string &error) const;
  EditorWorkspace &scene_;
  std::filesystem::path projectPath_;
  std::unique_ptr<EditorWorkspace> prefab_;
  std::unique_ptr<EditorWorkspace> terrain_;
  std::unique_ptr<EditorWorkspace> hud_;
  EditorDocumentSession focused_ = EditorDocumentSession::Scene;
};
} // namespace demi::editor
