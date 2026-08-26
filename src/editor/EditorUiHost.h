#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "demi/runtime/scene/model/SceneTypes.h"
#include "editor/EditorSceneView2DState.h"
#include "editor/EditorSceneViewState.h"

namespace demi::runtime {
struct World;
}

namespace demi::editor {

struct EditorViewportArea {
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
};

class EditorUiHost {
public:
  virtual ~EditorUiHost() = default;

  EditorUiHost(const EditorUiHost &) = delete;
  EditorUiHost &operator=(const EditorUiHost &) = delete;

  [[nodiscard]] virtual bool initialize(std::string title,
                                        std::string &error) = 0;
  virtual void shutdown() = 0;
  [[nodiscard]] virtual bool beginFrame(std::string &error) = 0;
  [[nodiscard]] virtual std::vector<std::filesystem::path>
  takeDroppedFiles() = 0;
  [[nodiscard]] virtual bool
  configureViewport(const std::filesystem::path &projectDirectory,
                    std::string &error) = 0;
  [[nodiscard]] virtual bool renderViewport(const runtime::World &world,
                                            EditorViewportArea area,
                                            const EditorSceneViewCamera &camera,
                                            std::string &error) = 0;
  [[nodiscard]] virtual bool
  renderViewport2D(const runtime::World &world, EditorViewportArea area,
                   const EditorSceneView2DCamera &camera, bool showColliders,
                   std::string &error) = 0;
  [[nodiscard]] virtual bool
  configureGameRenderer(const std::filesystem::path &projectDirectory,
                        std::string &error) = 0;
  [[nodiscard]] virtual bool prepareGameTarget(EditorViewportArea area,
                                               std::string &error) = 0;
  virtual void releaseGameRenderer() = 0;
  [[nodiscard]] virtual bool renderGame(const runtime::World &world,
                                        EditorViewportArea area,
                                        float interpolationAlpha,
                                        std::string &error) = 0;
  [[nodiscard]] virtual runtime::InputState gameInput(EditorViewportArea area,
                                                      bool focused) const = 0;
  [[nodiscard]] virtual std::uint16_t gameTextureIndex() const = 0;
  [[nodiscard]] virtual float deltaSeconds() const = 0;
  [[nodiscard]] virtual bool setViewportInputCaptured(bool captured,
                                                      std::string &error) = 0;
  virtual void endFrame() = 0;
  [[nodiscard]] virtual bool shouldClose() const = 0;
  virtual void acknowledgeCloseRequest() = 0;
  [[nodiscard]] virtual int width() const = 0;
  [[nodiscard]] virtual int height() const = 0;
  [[nodiscard]] virtual std::string rendererName() const = 0;

protected:
  EditorUiHost() = default;
};

[[nodiscard]] std::unique_ptr<EditorUiHost> createEditorUiHost();

} // namespace demi::editor
