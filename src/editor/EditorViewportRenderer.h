#pragma once

#include "editor/EditorUiHost.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace demi::runtime {
struct World;
namespace render {
class BgfxRenderer2D;
class BgfxRenderer3D;
class GpuResources;
class RenderCommands;
struct RenderTargetHandles;
} // namespace render
namespace ui {
struct UiDocument;
}
} // namespace demi::runtime

namespace demi::editor {

// Owns the authored Scene/HUD viewport renderers and offscreen target.
class EditorViewportRenderer {
public:
  EditorViewportRenderer(runtime::render::GpuResources &resources,
                         runtime::render::RenderCommands &commands);
  ~EditorViewportRenderer();

  EditorViewportRenderer(const EditorViewportRenderer &) = delete;
  EditorViewportRenderer &operator=(const EditorViewportRenderer &) = delete;

  [[nodiscard]] bool configure(const std::filesystem::path &projectDirectory,
                               std::string &error);
  void release();
  [[nodiscard]] bool prepareTarget(EditorViewportArea area, std::string &error);
  [[nodiscard]] bool render3D(const runtime::World &world,
                              EditorViewportArea area,
                              const EditorSceneViewCamera &camera,
                              float deltaSeconds, std::string &error);
  [[nodiscard]] bool render2D(const runtime::World &world,
                              EditorViewportArea area,
                              const EditorSceneView2DCamera &camera,
                              bool showColliders, float deltaSeconds,
                              std::string &error);
  [[nodiscard]] bool renderHud(const runtime::ui::UiDocument &document,
                               EditorViewportArea area, float deltaSeconds,
                               std::string &error);
  [[nodiscard]] std::uint16_t textureIndex() const;

private:
  [[nodiscard]] bool targetReady(EditorViewportArea area, std::string &error);
  [[nodiscard]] bool ensureTarget(std::uint16_t width, std::uint16_t height,
                                  std::string &error);
  void destroyTarget();

  runtime::render::GpuResources &resources_;
  runtime::render::RenderCommands &commands_;
  std::unique_ptr<runtime::render::BgfxRenderer3D> renderer3D_;
  std::unique_ptr<runtime::render::BgfxRenderer2D> renderer2D_;
  std::unique_ptr<runtime::render::RenderTargetHandles> target_;
  std::uint16_t targetWidth_ = 0;
  std::uint16_t targetHeight_ = 0;
};

} // namespace demi::editor
