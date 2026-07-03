#include "demi/runtime/Renderer3D.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

namespace {

::Color toRlColor(const Color& value) {
  return ::Color{
    static_cast<unsigned char>(std::round(std::clamp(value.r, 0.0F, 1.0F) * 255.0F)),
    static_cast<unsigned char>(std::round(std::clamp(value.g, 0.0F, 1.0F) * 255.0F)),
    static_cast<unsigned char>(std::round(std::clamp(value.b, 0.0F, 1.0F) * 255.0F)),
    static_cast<unsigned char>(std::round(std::clamp(value.a, 0.0F, 1.0F) * 255.0F)),
  };
}

::Vector3 toRlVec3(const Vec3& value) {
  return ::Vector3{.x = value.x, .y = value.y, .z = value.z};
}

struct ImageData {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
};

std::optional<std::string> nextPpmToken(std::istream& input) {
  std::string token;
  while (input >> token) {
    if (!token.empty() && token[0] == '#') {
      std::string ignored;
      std::getline(input, ignored);
      continue;
    }
    return token;
  }
  return std::nullopt;
}

std::optional<ImageData> loadPpm(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return std::nullopt;
  }

  const std::optional<std::string> magic = nextPpmToken(input);
  if (!magic.has_value() || *magic != "P3") {
    return std::nullopt;
  }

  const std::optional<std::string> widthText = nextPpmToken(input);
  const std::optional<std::string> heightText = nextPpmToken(input);
  const std::optional<std::string> maxText = nextPpmToken(input);
  if (!widthText.has_value() || !heightText.has_value() || !maxText.has_value()) {
    return std::nullopt;
  }

  ImageData image;
  try {
    image.width = std::stoi(*widthText);
    image.height = std::stoi(*heightText);
    const int maxValue = std::stoi(*maxText);
    if (image.width <= 0 || image.height <= 0 || maxValue <= 0) {
      return std::nullopt;
    }

    image.pixels.reserve(static_cast<std::size_t>(image.width * image.height * 4));
    for (int i = 0; i < image.width * image.height; ++i) {
      const std::optional<std::string> rText = nextPpmToken(input);
      const std::optional<std::string> gText = nextPpmToken(input);
      const std::optional<std::string> bText = nextPpmToken(input);
      if (!rText.has_value() || !gText.has_value() || !bText.has_value()) {
        return std::nullopt;
      }

      const auto scale = [maxValue](const int value) {
        return static_cast<unsigned char>(std::clamp(value * 255 / maxValue, 0, 255));
      };
      const unsigned char r = scale(std::stoi(*rText));
      const unsigned char g = scale(std::stoi(*gText));
      const unsigned char b = scale(std::stoi(*bText));
      const unsigned char a = 255U;
      image.pixels.push_back(r);
      image.pixels.push_back(g);
      image.pixels.push_back(b);
      image.pixels.push_back(a);
    }
  } catch (...) {
    return std::nullopt;
  }

  return image;
}

void drawMeshEntity(const Entity& entity, const std::unordered_map<std::string, Texture2D>& textures) {
  if (!entity.meshRenderer.has_value() || !entity.transform3D.has_value()) {
    return;
  }

  const Transform3DComponent& transform = *entity.transform3D;
  const MeshRendererComponent& mesh = *entity.meshRenderer;
  const ::Vector3 position = toRlVec3(transform.position);
  const ::Vector3 size = toRlVec3(mesh.size);
  const ::Color color = toRlColor(mesh.color);

  const ::Vector3 rotation = toRlVec3(transform.rotation);
  const float rotationX = rotation.x * (180.0F / 3.14159265358979F);
  const float rotationY = rotation.y * (180.0F / 3.14159265358979F);
  const float rotationZ = rotation.z * (180.0F / 3.14159265358979F);

  Texture2D texture{};
  bool hasTexture = false;
  if (!mesh.texture.empty()) {
    const auto found = textures.find(mesh.texture);
    if (found != textures.end()) {
      texture = found->second;
      hasTexture = true;
    }
  }

  const auto drawShape = [&](const Texture2D& tex, bool textured) {
    if (mesh.shape == "sphere") {
      if (textured) {
        DrawSphereEx(position, size.x * 0.5F, 16, 16, color);
      } else {
        DrawSphere(position, size.x * 0.5F, color);
      }
    } else if (mesh.shape == "plane") {
      const ::Rectangle source{.x = 0, .y = 0, .width = static_cast<float>(textured ? tex.width : 1), .height = static_cast<float>(textured ? tex.height : 1)};
      DrawPlane(position, {size.x, size.z}, color);
      (void)source;
    } else if (mesh.shape == "cylinder") {
      DrawCylinder(position, size.x * 0.5F, size.x * 0.5F, size.y, 16, color);
    } else {
      DrawCubeV(position, size, color);
    }
  };

  rlPushMatrix();
  rlTranslatef(position.x, position.y, position.z);
  rlRotatef(rotationX, 1.0F, 0.0F, 0.0F);
  rlRotatef(rotationY, 0.0F, 1.0F, 0.0F);
  rlRotatef(rotationZ, 0.0F, 0.0F, 1.0F);
  rlTranslatef(-position.x, -position.y, -position.z);

  drawShape(texture, hasTexture);

  rlPopMatrix();

  if (mesh.wireframe) {
    if (mesh.shape == "sphere") {
      DrawSphereWires(position, size.x * 0.5F, 16, 16, {245, 245, 245, 255});
    } else if (mesh.shape == "cylinder") {
      DrawCylinderWires(position, size.x * 0.5F, size.x * 0.5F, size.y, 16, {245, 245, 245, 255});
    } else {
      DrawCubeWiresV(position, size, {245, 245, 245, 255});
    }
  }

  if (entity.boxCollider3D.has_value()) {
    const ::Vector3 colliderCenter{
      .x = position.x + entity.boxCollider3D->offset.x,
      .y = position.y + entity.boxCollider3D->offset.y,
      .z = position.z + entity.boxCollider3D->offset.z,
    };
    const ::Vector3 colliderSize = toRlVec3(entity.boxCollider3D->size);
    DrawCubeWiresV(colliderCenter, colliderSize, {244, 91, 105, 255});
  }
}

} // namespace

Renderer3D::~Renderer3D() {
  for (auto& [id, texture] : textures_) {
    (void)id;
    UnloadTexture(texture);
  }
}

void Renderer3D::loadTextureAssets(const AssetRegistry& registry) {
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type != "Texture2D") {
      continue;
    }

    const std::optional<ImageData> image = loadPpm(asset.sourcePath);
    if (!image.has_value()) {
      std::cerr << "Texture load failed for " << asset.id << " from " << asset.sourcePath.string() << ". Using fallback material.\n";
      continue;
    }

    ::Image rlImage{
      .data = const_cast<void*>(static_cast<const void*>(image->pixels.data())),
      .width = image->width,
      .height = image->height,
      .mipmaps = 1,
      .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D texture = LoadTextureFromImage(rlImage);
    if (texture.id == 0) {
      std::cerr << "LoadTextureFromImage failed for " << asset.id << ". Using fallback material.\n";
      continue;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
    textures_[asset.id] = texture;
  }
}

void Renderer3D::beginFrame(const Camera3DComponent& camera, const Vec3 cameraPosition, const int width, const int height) {
  camera_ = camera;
  cameraPosition_ = cameraPosition;
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);

  BeginDrawing();
  ClearBackground(toRlColor(camera.clearColor));

  const ::Vector3 position = toRlVec3(cameraPosition);
  const ::Vector3 target = {
    position.x + camera.targetOffset.x,
    position.y + camera.targetOffset.y,
    position.z + camera.targetOffset.z,
  };
  const ::Vector3 up = {0.0F, 1.0F, 0.0F};
  ::Camera3D rlCamera{
    .position = position,
    .target = target,
    .up = up,
    .fovy = std::max(camera.fov, 1.0F),
    .projection = camera.perspective ? CAMERA_PERSPECTIVE : CAMERA_ORTHOGRAPHIC,
  };

  BeginMode3D(rlCamera);
}

void Renderer3D::drawWorld(const World& world) {
  for (const Entity& entity : world.entities) {
    if (entity.directionalLight.has_value()) {
      const ::Vector3 direction = toRlVec3(entity.directionalLight->direction);
      const ::Color color = toRlColor(entity.directionalLight->color);
      DrawLine3D({0, 0, 0}, direction, color);
    }
  }

  for (const Entity& entity : world.entities) {
    if (entity.meshRenderer.has_value() || entity.boxCollider3D.has_value()) {
      drawMeshEntity(entity, textures_);
    }
  }

  DrawGrid(20, 1.0F);
}

void Renderer3D::drawHud(const World& world) {
  EndMode3D();

  const float canvasWidth = std::max(world.hudCanvasSize.x, 1.0F);
  const float canvasHeight = std::max(world.hudCanvasSize.y, 1.0F);
  const float scaleX = static_cast<float>(width_) / canvasWidth;
  const float scaleY = static_cast<float>(height_) / canvasHeight;

  for (const HudRectElement& element : world.hudRects) {
    if (!element.visible) {
      continue;
    }
    ::Rectangle rect{
      .x = element.position.x * scaleX,
      .y = element.position.y * scaleY,
      .width = element.size.x * scaleX,
      .height = element.size.y * scaleY,
    };
    DrawRectangleRec(rect, toRlColor(element.color));
  }

  for (const HudTextElement& element : world.hudText) {
    if (!element.visible) {
      continue;
    }
    const int fontSize = std::max(static_cast<int>(element.scale * 6), 8);
    DrawText(element.text.c_str(),
             static_cast<int>(element.position.x * scaleX),
             static_cast<int>(element.position.y * scaleY),
             fontSize,
             toRlColor(element.color));
  }

  for (const HudButtonElement& element : world.hudButtons) {
    if (!element.visible) {
      continue;
    }
    const ::Color color = toRlColor(element.hovered ? element.hoverColor : element.color);
    ::Rectangle rect{
      .x = element.position.x * scaleX,
      .y = element.position.y * scaleY,
      .width = element.size.x * scaleX,
      .height = element.size.y * scaleY,
    };
    DrawRectangleRec(rect, color);
    const int fontSize = std::max(static_cast<int>(element.scale * 6), 8);
    const ::Vector2 measured = MeasureTextEx(GetFontDefault(), element.label.c_str(), static_cast<float>(fontSize), 1.0F);
    DrawText(element.label.c_str(),
             static_cast<int>(rect.x + rect.width * 0.5F - measured.x * 0.5F),
             static_cast<int>(rect.y + rect.height * 0.5F - measured.y * 0.5F),
             fontSize,
             toRlColor(element.textColor));
  }
}

void Renderer3D::endFrame() {
  EndDrawing();
}

} // namespace demi::runtime
