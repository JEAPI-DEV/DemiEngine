#include "demi/runtime/render/Renderer3D.h"

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

void drawTexturedPlane(const ::Vector3 center, const ::Vector2 size, const Texture2D& texture, const ::Color tint) {
  const float halfX = size.x * 0.5F;
  const float halfZ = size.y * 0.5F;
  rlSetTexture(texture.id);
  rlBegin(RL_QUADS);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);
  rlNormal3f(0.0F, 1.0F, 0.0F);
  rlTexCoord2f(0.0F, 0.0F);
  rlVertex3f(center.x - halfX, center.y, center.z - halfZ);
  rlTexCoord2f(1.0F, 0.0F);
  rlVertex3f(center.x + halfX, center.y, center.z - halfZ);
  rlTexCoord2f(1.0F, 1.0F);
  rlVertex3f(center.x + halfX, center.y, center.z + halfZ);
  rlTexCoord2f(0.0F, 1.0F);
  rlVertex3f(center.x - halfX, center.y, center.z + halfZ);
  rlEnd();
  rlSetTexture(0);
}

void drawTexturedCube(const ::Vector3 center, const ::Vector3 size, const Texture2D& texture, const ::Color tint) {
  const float x0 = center.x - size.x * 0.5F;
  const float x1 = center.x + size.x * 0.5F;
  const float y0 = center.y - size.y * 0.5F;
  const float y1 = center.y + size.y * 0.5F;
  const float z0 = center.z - size.z * 0.5F;
  const float z1 = center.z + size.z * 0.5F;

  const auto face = [&](const ::Vector3 normal, const ::Vector3 a, const ::Vector3 b, const ::Vector3 c, const ::Vector3 d) {
    rlNormal3f(normal.x, normal.y, normal.z);
    rlTexCoord2f(0.0F, 0.0F);
    rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(1.0F, 0.0F);
    rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(1.0F, 1.0F);
    rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(0.0F, 1.0F);
    rlVertex3f(d.x, d.y, d.z);
  };

  rlSetTexture(texture.id);
  rlBegin(RL_QUADS);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);
  face({0.0F, 0.0F, 1.0F}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});
  face({0.0F, 0.0F, -1.0F}, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0});
  face({1.0F, 0.0F, 0.0F}, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1});
  face({-1.0F, 0.0F, 0.0F}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0});
  face({0.0F, 1.0F, 0.0F}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0});
  face({0.0F, -1.0F, 0.0F}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1});
  rlEnd();
  rlSetTexture(0);
}

void drawMeshEntity(const World& world, const Entity& entity, const std::unordered_map<std::string, Texture2D>& textures, const std::unordered_map<std::string, Model>& models) {
  if (!entity.meshRenderer.has_value() || !entity.transform3D.has_value()) {
    return;
  }

  const MeshRendererComponent& mesh = *entity.meshRenderer;
  const ::Vector3 position = toRlVec3(worldPosition3D(world, entity));
  const ::Vector3 size = toRlVec3(mesh.size);
  const ::Color color = toRlColor(mesh.color);

  const ::Vector3 rotation = toRlVec3(worldRotation3D(world, entity));
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
  const Model* model = nullptr;
  if (!mesh.model.empty()) {
    const auto found = models.find(mesh.model);
    if (found != models.end()) {
      model = &found->second;
    }
  }

  const auto drawShape = [&](const Texture2D& tex, bool textured) {
    if (mesh.shape == "sphere") {
      DrawSphere(position, size.x * 0.5F, color);
    } else if (mesh.shape == "plane") {
      if (textured) {
        drawTexturedPlane(position, {size.x, size.z}, tex, color);
      } else {
        DrawPlane(position, {size.x, size.z}, color);
      }
    } else if (mesh.shape == "cylinder") {
      DrawCylinder(position, size.x * 0.5F, size.x * 0.5F, size.y, 16, color);
    } else if (textured) {
      drawTexturedCube(position, size, tex, color);
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

  if (model != nullptr) {
    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlScalef(size.x, size.y, size.z);
    DrawModel(*model, {0.0F, 0.0F, 0.0F}, 1.0F, color);
    rlPopMatrix();
  } else {
    drawShape(texture, hasTexture);
  }

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
  if (entity.sphereCollider3D.has_value()) {
    const ::Vector3 colliderCenter{
      .x = position.x + entity.sphereCollider3D->offset.x,
      .y = position.y + entity.sphereCollider3D->offset.y,
      .z = position.z + entity.sphereCollider3D->offset.z,
    };
    DrawSphereWires(colliderCenter, entity.sphereCollider3D->radius, 16, 16, {244, 91, 105, 255});
  }
}

} // namespace

Renderer3D::~Renderer3D() {
  for (auto& [id, texture] : textures_) {
    (void)id;
    UnloadTexture(texture);
  }
  for (auto& [id, model] : models_) {
    (void)id;
    UnloadModel(model);
  }
  for (auto& [id, texture] : modelTextures_) {
    (void)id;
    UnloadTexture(texture);
  }
}

void Renderer3D::loadTextureAssets(const AssetRegistry& registry) {
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type == "Model3D") {
      Model model = LoadModel(asset.sourcePath.string().c_str());
      if (model.meshCount <= 0) {
        std::cerr << "Model load failed for " << asset.id << " from " << asset.sourcePath.string() << ".\n";
        continue;
      }
      if (asset.texturePath.has_value()) {
        Texture2D texture = LoadTexture(asset.texturePath->string().c_str());
        if (texture.id == 0) {
          std::cerr << "Model texture load failed for " << asset.id << " from " << asset.texturePath->string() << ".\n";
        } else if (model.materialCount > 0) {
          SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
          SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE, texture);
          for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
            model.meshMaterial[meshIndex] = 0;
          }
          modelTextures_[asset.id] = texture;
        } else {
          UnloadTexture(texture);
        }
      }
      models_[asset.id] = model;
      continue;
    }

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

void Renderer3D::beginFrame(const Camera3DComponent& camera, const Vec3 cameraPosition, const Vec3 cameraRotation, const int width, const int height) {
  camera_ = camera;
  cameraPosition_ = cameraPosition;
  cameraRotation_ = cameraRotation;
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);

  BeginDrawing();
  ClearBackground(toRlColor(camera.clearColor));

  const ::Vector3 position = toRlVec3(cameraPosition);
  const Vec3 rotatedTargetOffset = rotateYaw(camera.targetOffset, cameraRotation.y);
  const ::Vector3 target = {
    position.x + rotatedTargetOffset.x,
    position.y + rotatedTargetOffset.y,
    position.z + rotatedTargetOffset.z,
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
    if (entity.meshRenderer.has_value() || entity.boxCollider3D.has_value() || entity.sphereCollider3D.has_value()) {
      drawMeshEntity(world, entity, textures_, models_);
    }
  }

  constexpr int slices = 40;
  constexpr float spacing = 1.0F;
  constexpr float half = static_cast<float>(slices) * spacing * 0.5F;
  constexpr float y = 0.0125F;
  const ::Color gridColor = {170, 188, 180, 110};
  for (int i = 0; i <= slices; ++i) {
    const float p = -half + static_cast<float>(i) * spacing;
    DrawLine3D({-half, y, p}, {half, y, p}, gridColor);
    DrawLine3D({p, y, -half}, {p, y, half}, gridColor);
  }
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
