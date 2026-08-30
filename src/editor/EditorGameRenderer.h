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
} // namespace demi::runtime

namespace demi::editor {

// Owns only the embedded Game view's GPU renderers and offscreen target.
class EditorGameRenderer {
public:
  EditorGameRenderer(runtime::render::GpuResources &resources,
                     runtime::render::RenderCommands &commands);
  ~EditorGameRenderer();

  EditorGameRenderer(const EditorGameRenderer &) = delete;
  EditorGameRenderer &operator=(const EditorGameRenderer &) = delete;

  [[nodiscard]] bool configure(const std::filesystem::path &projectDirectory,
                               std::string &error);
  void release();
  [[nodiscard]] bool prepareTarget(EditorViewportArea area, std::string &error);
  [[nodiscard]] bool render(const runtime::World &world,
                            EditorViewportArea area, float deltaSeconds,
                            float interpolationAlpha, std::string &error);
  [[nodiscard]] std::uint16_t textureIndex() const;

private:
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
