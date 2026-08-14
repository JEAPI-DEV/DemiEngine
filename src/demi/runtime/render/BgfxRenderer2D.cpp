#include "demi/runtime/render/BgfxRenderer2D.h"

#include "demi/runtime/render/ParticleSystem2D.h"
#include "demi/runtime/render/backend/RenderAssetLoading.h"
#include "demi/runtime/render/backend/SvgDecoder2D.h"
#include "demi/runtime/render/bgfx2d/ColliderCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx2d/DebugCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/IsoCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/ParticleCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/SpriteCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/TilemapCanvasRenderer.h"
#include "demi/runtime/render/bgfx2d/UiCanvasRenderer.h"

#include <span>

namespace demi::runtime::render {

class BgfxRenderer2D::ParticleSimulation {
public:
  ParticleSystem2D system;
};

namespace {

bool isRasterTexture(const AssetManifest &asset) {
  return asset.type == "Texture2D";
}

bool isSvgTexture(const AssetManifest &asset) {
  return asset.type == "Icon2D" || asset.type == "SvgTexture2D";
}

} // namespace

BgfxRenderer2D::BgfxRenderer2D(GpuResources &resources,
                               RenderCommands &commands)
    : canvas_(resources, commands), font_(resources), textures_(resources),
      materials_(resources),
      particles_(std::make_unique<ParticleSimulation>()) {}

BgfxRenderer2D::~BgfxRenderer2D() { shutdown(); }

bool BgfxRenderer2D::initialize(std::string &error) {
  if (initialized_)
    return true;
  if (!canvas_.initialize(error))
    return false;
  if (!font_.initializeDefault(48.0F, error)) {
    canvas_.shutdown();
    return false;
  }
  initialized_ = true;
  return true;
}

void BgfxRenderer2D::shutdown() {
  frameOpen_ = false;
  particles_->system.clear();
  tilemaps_.clear();
  textureAnimations_.clear();
  textures_.clear();
  materials_.clear();
  externalTextures_.clear();
  font_.shutdown();
  canvas_.shutdown();
  initialized_ = false;
}

bool BgfxRenderer2D::loadAssets(const AssetRegistry &registry,
                                std::vector<std::string> &diagnostics) {
  textures_.clear();
  externalTextures_.clear();
  tilemaps_.clear();
  textureAnimations_.clear();
  bool success = materials_.load(registry, diagnostics);
  std::string fontError;
  font_.shutdown();
  if (!font_.initializeDefault(48.0F, fontError)) {
    diagnostics.push_back("asset://fonts/default: " + fontError);
    return false;
  }
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type == "Font2D") {
      const auto bytes = readRenderAssetBytes(asset.sourcePath);
      std::uint64_t revision = 1469598103934665603ULL;
      for (const unsigned char byte : asset.sourceHash) {
        revision ^= byte;
        revision *= 1099511628211ULL;
      }
      std::string error;
      if (bytes.empty() ||
          !font_.addFallback(asset.id, bytes, revision, error)) {
        diagnostics.push_back(
            asset.id + ": " +
            (error.empty() ? "could not read source" : error));
        success = false;
      }
      continue;
    }
    if (asset.type == "Tilemap2D") {
      std::string error;
      auto tilemap = loadTilemapAsset(asset, error);
      if (!tilemap) {
        diagnostics.push_back(asset.id + ": " + error);
        success = false;
      } else {
        tilemaps_.insert_or_assign(asset.id, std::move(*tilemap));
      }
      continue;
    }
    if (asset.type == "ImageAnimation2D") {
      for (std::size_t frame = 0; frame < asset.sourcePaths.size(); ++frame) {
        const auto bytes = readRenderAssetBytes(asset.sourcePaths[frame]);
        std::string error;
        if (bytes.empty() ||
            !textures_.load(asset.id + "#" + std::to_string(frame), bytes,
                            error,
                            textureSampling2D(asset, TextureFilter::Nearest))) {
          diagnostics.push_back(
              asset.id + " frame " + std::to_string(frame) + ": " +
              (error.empty() ? "could not read source" : error));
          success = false;
        }
      }
      textureAnimations_[asset.id] = {
          .frameCount = asset.sourcePaths.size(),
          .frameDurations = {},
      };
      continue;
    }
    if (asset.type == "GifAnimation2D") {
      const auto bytes = readRenderAssetBytes(asset.sourcePath);
      AnimatedImageData2D animation;
      std::string error;
      if (bytes.empty() || !decodeGif2D(bytes, animation, error)) {
        diagnostics.push_back(
            asset.id + ": " +
            (error.empty() ? "could not read source" : error));
        success = false;
        continue;
      }
      bool uploaded = true;
      for (std::size_t frame = 0; frame < animation.frames.size(); ++frame) {
        if (!textures_.upload(
                asset.id + "#" + std::to_string(frame), animation.frames[frame],
                error, textureSampling2D(asset, TextureFilter::Nearest))) {
          diagnostics.push_back(asset.id + " frame " + std::to_string(frame) +
                                ": " + error);
          uploaded = false;
          success = false;
          break;
        }
      }
      if (uploaded)
        textureAnimations_[asset.id] = {
            .frameCount = animation.frames.size(),
            .frameDurations = std::move(animation.frameDurations),
        };
      continue;
    }
    if (isSvgTexture(asset)) {
      ImageData2D image;
      std::string error;
      if (!decodeSvg2D(asset.sourcePath, asset.type == "Icon2D", image,
                       error) ||
          !textures_.upload(asset.id, image, error,
                            textureSampling2D(asset, TextureFilter::Linear))) {
        diagnostics.push_back(asset.id + ": " + error);
        success = false;
      }
      continue;
    }
    if (!isRasterTexture(asset))
      continue;
    const auto bytes = readRenderAssetBytes(asset.sourcePath);
    std::string error;
    if (bytes.empty() ||
        !textures_.load(asset.id, bytes, error,
                        textureSampling2D(asset, TextureFilter::Nearest))) {
      diagnostics.push_back(asset.id + ": " +
                            (error.empty() ? "could not read source" : error));
      success = false;
    }
  }
  return success;
}

bool BgfxRenderer2D::beginFrame(const Camera2DComponent &camera,
                                const Vec2 cameraPosition,
                                const std::uint16_t viewportWidth,
                                const std::uint16_t viewportHeight,
                                const float deltaSeconds, std::string &error,
                                const float physicsInterpolationAlpha) {
  if (!initialized_) {
    error = "BgfxRenderer2D must be initialized before beginning a frame.";
    return false;
  }
  if (frameOpen_) {
    error = "BgfxRenderer2D cannot begin a second frame before ending one.";
    return false;
  }
  camera_ = camera;
  cameraPosition_ = cameraPosition;
  viewportWidth_ = std::max<std::uint16_t>(viewportWidth, 1);
  viewportHeight_ = std::max<std::uint16_t>(viewportHeight, 1);
  deltaSeconds_ = std::max(deltaSeconds, 0.0F);
  physicsInterpolationAlpha_ =
      std::clamp(physicsInterpolationAlpha, 0.0F, 1.0F);
  animationTime_ += deltaSeconds_;
  frameOpen_ = canvas_.begin(0, viewportWidth_, viewportHeight_,
                             packClearColorRgba8(camera.clearColor), error);
  return frameOpen_;
}

bool BgfxRenderer2D::beginOverlay(const std::uint16_t viewId,
                                  const std::uint16_t viewportWidth,
                                  const std::uint16_t viewportHeight,
                                  const float deltaSeconds,
                                  std::string &error) {
  return beginOverlayRegion(viewId, 0, 0, viewportWidth, viewportHeight,
                            deltaSeconds, error);
}

bool BgfxRenderer2D::beginOverlayRegion(
    const std::uint16_t viewId, const std::uint16_t x, const std::uint16_t y,
    const std::uint16_t viewportWidth, const std::uint16_t viewportHeight,
    const float deltaSeconds, std::string &error,
    const FrameBufferHandle frameBuffer) {
  if (!initialized_) {
    error = "BgfxRenderer2D must be initialized before beginning an overlay.";
    return false;
  }
  if (frameOpen_) {
    error = "BgfxRenderer2D cannot begin an overlay while a frame is open.";
    return false;
  }
  viewportWidth_ = std::max<std::uint16_t>(viewportWidth, 1);
  viewportHeight_ = std::max<std::uint16_t>(viewportHeight, 1);
  deltaSeconds_ = std::max(deltaSeconds, 0.0F);
  animationTime_ += deltaSeconds_;
  frameOpen_ = canvas_.begin(viewId, viewportWidth_, viewportHeight_, 0, error,
                             false, x, y, frameBuffer);
  return frameOpen_;
}

void BgfxRenderer2D::setExternalTexture(std::string id,
                                        const TextureView2D texture) {
  if (texture.handle)
    externalTextures_.insert_or_assign(std::move(id), texture);
}

bool BgfxRenderer2D::drawWorld(const World &world) {
  if (!frameOpen_)
    return false;
  TilemapCanvasRenderer tilemapRenderer(canvas_, textures_, tilemaps_);
  IsoCanvasRenderer isoRenderer(canvas_, textures_);
  ColliderCanvasRenderer colliderRenderer(canvas_);
  SpriteCanvasRenderer spriteRenderer(canvas_, textures_, &textureAnimations_,
                                      &materials_);
  ParticleCanvasRenderer particleRenderer(canvas_, textures_);
  DebugCanvasRenderer debugRenderer(canvas_, &font_);
  if (!tilemapRenderer.draw(world, camera_, cameraPosition_, viewportWidth_,
                            viewportHeight_, animationTime_) ||
      !isoRenderer.draw(world, camera_, cameraPosition_, viewportWidth_,
                        viewportHeight_) ||
      !colliderRenderer.draw(world, camera_, cameraPosition_, viewportWidth_,
                             viewportHeight_) ||
      !spriteRenderer.draw(world, camera_, cameraPosition_, viewportWidth_,
                           viewportHeight_, animationTime_,
                           physicsInterpolationAlpha_))
    return false;
  particles_->system.update(world, deltaSeconds_);
  const auto particleData = particles_->system.renderData();
  return particleRenderer.draw(particleData, camera_, cameraPosition_,
                               viewportWidth_, viewportHeight_) &&
         debugRenderer.drawWorld(world, camera_, cameraPosition_,
                                 viewportWidth_, viewportHeight_);
}

bool BgfxRenderer2D::drawNavigation(const navigation::NavigationGrid2D &grid) {
  if (!frameOpen_)
    return false;
  return DebugCanvasRenderer(canvas_, &font_)
      .drawNavigation(grid, camera_, cameraPosition_, viewportWidth_,
                      viewportHeight_);
}

bool BgfxRenderer2D::drawHud(const World &world) { return drawUi(world.ui); }

bool BgfxRenderer2D::drawUi(const ui::UiDocument &document) {
  if (!frameOpen_)
    return false;
  return UiCanvasRenderer(
             canvas_, font_,
             [this](const std::string_view id) {
               const auto external = externalTextures_.find(std::string(id));
               return external != externalTextures_.end() ? external->second
                                                          : textures_.find(id);
             },
             &textureAnimations_, animationTime_)
      .draw(document, viewportWidth_, viewportHeight_);
}

bool BgfxRenderer2D::endFrame(std::string &error) {
  if (!frameOpen_) {
    error = "BgfxRenderer2D has no open frame to end.";
    return false;
  }
  frameOpen_ = false;
  return canvas_.flush(error);
}

} // namespace demi::runtime::render
