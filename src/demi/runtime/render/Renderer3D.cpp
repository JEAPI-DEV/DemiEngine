#include "demi/runtime/render/Renderer3D.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

Vec3 add(const Vec3 a, const Vec3 b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(const Vec3 a, const Vec3 b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(const Vec3 a, const Vec3 b) {
  return Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 scale(const Vec3 value, const float amount) {
  return Vec3{value.x * amount, value.y * amount, value.z * amount};
}

float dot(const Vec3 a, const Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3 a, const Vec3 b) {
  return Vec3{
    .x = a.y * b.z - a.z * b.y,
    .y = a.z * b.x - a.x * b.z,
    .z = a.x * b.y - a.y * b.x,
  };
}

float length(const Vec3 value) {
  return std::sqrt(dot(value, value));
}

Vec3 normalize(const Vec3 value) {
  const float valueLength = length(value);
  if (valueLength <= 0.0001F) {
    return Vec3{0.0F, 0.0F, -1.0F};
  }
  return scale(value, 1.0F / valueLength);
}

Vec3 rotatePitchYaw(const Vec3 value, const Vec3 rotation) {
  const float sinX = std::sin(rotation.x);
  const float cosX = std::cos(rotation.x);
  const Vec3 pitched{
    .x = value.x,
    .y = value.y * cosX - value.z * sinX,
    .z = value.y * sinX + value.z * cosX,
  };
  return rotateYaw(pitched, rotation.y);
}

float horizontalRadiusForBounds(const Vec3 min, const Vec3 max, const Vec3 scaleValue) {
  const Vec3 extents = multiply(scale(subtract(max, min), 0.5F), scaleValue);
  return length(Vec3{std::abs(extents.x), std::abs(extents.y), std::abs(extents.z)});
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

std::optional<std::filesystem::path> findShaderPath(const std::filesystem::path& relativePath) {
  std::filesystem::path cursor = std::filesystem::current_path();
  while (true) {
    const std::filesystem::path candidate = cursor / relativePath;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    if (!cursor.has_parent_path() || cursor == cursor.parent_path()) {
      break;
    }
    cursor = cursor.parent_path();
  }
  return std::nullopt;
}

Shader loadAlphaCutoutShader() {
  const std::optional<std::filesystem::path> vertexShader = findShaderPath("src/demi/runtime/render/shaders/alpha_cutout.vs");
  const std::optional<std::filesystem::path> fragmentShader = findShaderPath("src/demi/runtime/render/shaders/alpha_cutout.fs");
  if (!vertexShader.has_value() || !fragmentShader.has_value()) {
    std::cerr << "Alpha cutout shader files were not found. Textured cutout meshes will use the default material.\n";
    return {};
  }

  Shader shader = LoadShader(vertexShader->string().c_str(), fragmentShader->string().c_str());
  shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
  shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
  shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
  shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader, "colDiffuse");
  return shader;
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

template <typename T>
std::size_t bytesHash(const std::vector<T>& values) {
  const auto* bytes = reinterpret_cast<const char*>(values.data());
  return std::hash<std::string_view>{}(std::string_view{bytes, values.size() * sizeof(T)});
}

std::string dynamicMeshSignature(const MeshRendererComponent& mesh, const bool alphaCutout) {
  if (mesh.vertices.empty()) {
    return {};
  }
  if (mesh.revision > 0) {
    return "revision:" + std::to_string(mesh.revision) + ":" + mesh.texture + ":cutout:" + std::to_string(alphaCutout);
  }
  return std::to_string(mesh.vertices.size()) + ":" + std::to_string(bytesHash(mesh.vertices)) + ":" + std::to_string(mesh.normals.size()) + ":" +
         std::to_string(bytesHash(mesh.normals)) + ":" + std::to_string(mesh.uvs.size()) + ":" + std::to_string(bytesHash(mesh.uvs)) + ":" + mesh.texture +
         ":cutout:" + std::to_string(alphaCutout);
}

Mesh uploadDynamicMesh(const MeshRendererComponent& source) {
  ProfileScope uploadScope("Renderer3D.dynamic_mesh_upload_total");
  Mesh mesh{};
  mesh.vertexCount = static_cast<int>(source.vertices.size());
  mesh.triangleCount = mesh.vertexCount / 3;
  if (mesh.vertexCount <= 0 || mesh.triangleCount <= 0) {
    return mesh;
  }

  const std::size_t vertexBytes = source.vertices.size() * 3 * sizeof(float);
  const std::size_t normalBytes = source.vertices.size() * 3 * sizeof(float);
  const std::size_t uvBytes = source.vertices.size() * 2 * sizeof(float);
  RuntimeProfiler::addBytes("Renderer3D.dynamic_mesh_cpu_pack", vertexBytes + normalBytes + uvBytes);

  {
    ProfileScope scope("Renderer3D.dynamic_mesh_cpu_pack");
    mesh.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vertexBytes)));
    mesh.normals = static_cast<float*>(MemAlloc(static_cast<unsigned int>(normalBytes)));
    mesh.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned int>(uvBytes)));

    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
      const Vec3& vertex = source.vertices[i];
      const Vec3 normal = i < source.normals.size() ? source.normals[i] : Vec3{0.0F, 1.0F, 0.0F};
      const Vec2 uv = i < source.uvs.size() ? source.uvs[i] : Vec2{};
      mesh.vertices[i * 3 + 0] = vertex.x;
      mesh.vertices[i * 3 + 1] = vertex.y;
      mesh.vertices[i * 3 + 2] = vertex.z;
      mesh.normals[i * 3 + 0] = normal.x;
      mesh.normals[i * 3 + 1] = normal.y;
      mesh.normals[i * 3 + 2] = normal.z;
      mesh.texcoords[i * 2 + 0] = uv.x;
      mesh.texcoords[i * 2 + 1] = uv.y;
    }
  }

  {
    ProfileScope scope("Renderer3D.dynamic_mesh_vram_upload");
    UploadMesh(&mesh, false);
  }
  return mesh;
}

Model* dynamicModelForEntity(const Entity& entity,
                             const MeshRendererComponent& mesh,
                             const std::unordered_map<std::string, Texture2D>& textures,
                             std::unordered_map<std::string, DynamicModelCacheEntry>& dynamicModels,
                             const Shader* alphaCutoutShader) {
  if (mesh.vertices.empty()) {
    return nullptr;
  }
  auto cached = dynamicModels.find(entity.id);
  const bool useAlphaCutout = alphaCutoutShader != nullptr && !mesh.texture.empty();
  if (mesh.revision > 0 && cached != dynamicModels.end() && cached->second.revision == mesh.revision && cached->second.texture == mesh.texture &&
      cached->second.hasModel && cached->second.signature == dynamicMeshSignature(mesh, useAlphaCutout)) {
    return &cached->second.model;
  }
  const std::string signature = dynamicMeshSignature(mesh, useAlphaCutout);
  if (signature.empty()) {
    return nullptr;
  }
  if (cached != dynamicModels.end() && cached->second.signature == signature && cached->second.hasModel) {
    return &cached->second.model;
  }
  if (cached != dynamicModels.end() && cached->second.hasModel) {
    ProfileScope scope("Renderer3D.unload_dynamic_model");
    UnloadModel(cached->second.model);
  }
  Mesh uploaded = uploadDynamicMesh(mesh);
  if (uploaded.vertexCount <= 0) {
    dynamicModels.erase(entity.id);
    return nullptr;
  }
  Model model{};
  {
    ProfileScope scope("Renderer3D.load_model_from_mesh");
    model = LoadModelFromMesh(uploaded);
  }
  if (!mesh.texture.empty() && model.materialCount > 0) {
    const auto texture = textures.find(mesh.texture);
    if (texture != textures.end()) {
      SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE, texture->second);
      if (useAlphaCutout) {
        model.materials[0].shader = *alphaCutoutShader;
      }
    }
  }
  cached = dynamicModels
             .insert_or_assign(entity.id,
                               DynamicModelCacheEntry{
                                 .signature = signature,
                                 .texture = mesh.texture,
                                 .revision = mesh.revision,
                                 .model = model,
                                 .hasModel = true,
                               })
             .first;
  return &cached->second.model;
}

bool meshEntityVisible(const World& world,
                       const Entity& entity,
                       const MeshRendererComponent& mesh,
                       const Vec3 camera,
                       const Vec3 cameraRotation,
                       const Camera3DComponent& cameraComponent,
                       const float aspectRatio) {
  if (!entity.transform3D.has_value() || !cameraComponent.perspective) {
    return true;
  }

  const Vec3 position = worldPosition3D(world, entity);
  const Vec3 scaleValue = entity.transform3D->scale;
  Vec3 center = position;
  float radius = length(scaleValue) * 0.5F;

  if (mesh.hasBounds) {
    const Vec3 localCenter = scale(add(mesh.boundsMin, mesh.boundsMax), 0.5F);
    center = add(position, multiply(localCenter, scaleValue));
    radius = horizontalRadiusForBounds(mesh.boundsMin, mesh.boundsMax, scaleValue);
  } else if (!mesh.model.empty()) {
    return true;
  } else if (mesh.shape == "sphere") {
    radius = std::abs(scaleValue.x) * 0.5F;
  } else {
    radius = length(multiply(mesh.size, scaleValue)) * 0.5F;
  }

  const Vec3 forward = normalize(rotatePitchYaw(cameraComponent.targetOffset, cameraRotation));
  Vec3 right = normalize(cross(forward, Vec3{0.0F, 1.0F, 0.0F}));
  if (length(right) <= 0.0001F) {
    right = Vec3{1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(right, forward));
  const Vec3 toCenter = subtract(center, camera);
  const float distanceAlongForward = dot(toCenter, forward);
  if (distanceAlongForward < -radius) {
    return false;
  }

  const float halfVerticalFov = (std::max(cameraComponent.fov, 1.0F) * std::numbers::pi_v<float> / 180.0F) * 0.5F;
  const float verticalLimit = std::max(distanceAlongForward, 0.0F) * std::tan(halfVerticalFov) + radius;
  const float horizontalLimit = verticalLimit * std::max(aspectRatio, 1.0F) + radius;
  return std::abs(dot(toCenter, up)) <= verticalLimit && std::abs(dot(toCenter, right)) <= horizontalLimit;
}

void drawMeshEntity(const World& world,
                    const Entity& entity,
                    const std::unordered_map<std::string, Texture2D>& textures,
                    const std::unordered_map<std::string, Model>& models,
                    std::unordered_map<std::string, DynamicModelCacheEntry>& dynamicModels,
                    const Shader* alphaCutoutShader) {
  if (!entity.meshRenderer.has_value() || !entity.transform3D.has_value()) {
    return;
  }

  const MeshRendererComponent& mesh = *entity.meshRenderer;
  const ::Vector3 position = toRlVec3(worldPosition3D(world, entity));
  const ::Vector3 size = toRlVec3(mesh.size);
  const ::Color color = toRlColor(mesh.color);
  const bool drawSolid = color.a > 0 && !mesh.wireframe;

  const ::Vector3 rotation = toRlVec3(worldRotation3D(world, entity));
  constexpr float RadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
  const float rotationX = rotation.x * RadiansToDegrees;
  const float rotationY = rotation.y * RadiansToDegrees;
  const float rotationZ = rotation.z * RadiansToDegrees;

  Texture2D texture{};
  bool hasTexture = false;
  if (!mesh.texture.empty()) {
    const auto found = textures.find(mesh.texture);
    if (found != textures.end()) {
      texture = found->second;
      hasTexture = true;
    }
  }
  Model* dynamicModel = nullptr;
  const Model* staticModel = nullptr;
  if (!mesh.vertices.empty()) {
    ProfileScope scope("Renderer3D.dynamic_model_lookup");
    dynamicModel = dynamicModelForEntity(entity, mesh, textures, dynamicModels, alphaCutoutShader);
  } else if (!mesh.model.empty()) {
    const auto found = models.find(mesh.model);
    if (found != models.end()) {
      staticModel = &found->second;
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

  if (drawSolid && (dynamicModel != nullptr || staticModel != nullptr)) {
    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlScalef(size.x, size.y, size.z);
    {
      ProfileScope scope("Renderer3D.draw_model");
      if (dynamicModel != nullptr) {
        if (alphaCutoutShader != nullptr && !mesh.texture.empty() && dynamicModel->materialCount > 0) {
          dynamicModel->materials[0].shader = *alphaCutoutShader;
        }
        DrawModel(*dynamicModel, {0.0F, 0.0F, 0.0F}, 1.0F, color);
      } else {
        DrawModel(*staticModel, {0.0F, 0.0F, 0.0F}, 1.0F, color);
      }
    }
    rlPopMatrix();
  } else if (drawSolid) {
    ProfileScope scope("Renderer3D.draw_shape");
    drawShape(texture, hasTexture);
  }

  rlPopMatrix();

  if (mesh.wireframe) {
    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(rotationX, 1.0F, 0.0F, 0.0F);
    rlRotatef(rotationY, 0.0F, 1.0F, 0.0F);
    rlRotatef(rotationZ, 0.0F, 0.0F, 1.0F);
    rlTranslatef(-position.x, -position.y, -position.z);

    if (dynamicModel != nullptr || staticModel != nullptr) {
      rlPushMatrix();
      rlTranslatef(position.x, position.y, position.z);
      rlScalef(size.x, size.y, size.z);
      if (dynamicModel != nullptr) {
        DrawModelWires(*dynamicModel, {0.0F, 0.0F, 0.0F}, 1.0F, color);
      } else {
        DrawModelWires(*staticModel, {0.0F, 0.0F, 0.0F}, 1.0F, color);
      }
      rlPopMatrix();
    } else if (mesh.shape == "sphere") {
      DrawSphereWires(position, size.x * 0.5F, 16, 16, color);
    } else if (mesh.shape == "cylinder") {
      DrawCylinderWires(position, size.x * 0.5F, size.x * 0.5F, size.y, 16, color);
    } else {
      DrawCubeWiresV(position, size, color);
    }
    rlPopMatrix();
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
  for (auto& [id, model] : dynamicModels_) {
    (void)id;
    if (model.hasModel) {
      UnloadModel(model.model);
    }
  }
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
  if (hasAlphaCutoutShader_) {
    UnloadShader(alphaCutoutShader_);
  }
}

void Renderer3D::loadTextureAssets(const AssetRegistry& registry) {
  ProfileScope scope("Renderer3D.load_texture_assets");
  if (!hasAlphaCutoutShader_) {
    alphaCutoutShader_ = loadAlphaCutoutShader();
    hasAlphaCutoutShader_ = alphaCutoutShader_.id != 0;
  }
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type == "Model3D") {
      Model model{};
      {
        ProfileScope modelScope("Renderer3D.load_model_asset");
        model = LoadModel(asset.sourcePath.string().c_str());
      }
      if (model.meshCount <= 0) {
        std::cerr << "Model load failed for " << asset.id << " from " << asset.sourcePath.string() << ".\n";
        continue;
      }
      if (asset.texturePath.has_value()) {
        Texture2D texture{};
        {
          ProfileScope textureScope("Renderer3D.load_model_texture");
          texture = LoadTexture(asset.texturePath->string().c_str());
        }
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

    Texture2D texture{};
    if (const std::optional<ImageData> image = loadPpm(asset.sourcePath)) {
      ::Image rlImage{
        .data = const_cast<void*>(static_cast<const void*>(image->pixels.data())),
        .width = image->width,
        .height = image->height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
      };
      {
        ProfileScope textureScope("Renderer3D.load_texture_from_image");
        texture = LoadTextureFromImage(rlImage);
      }
    } else {
      {
        ProfileScope textureScope("Renderer3D.load_texture_file");
        texture = LoadTexture(asset.sourcePath.string().c_str());
      }
    }
    if (texture.id == 0) {
      std::cerr << "Texture load failed for " << asset.id << " from " << asset.sourcePath.string() << ". Using fallback material.\n";
      continue;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    textures_[asset.id] = texture;
  }
}

void Renderer3D::beginFrame(const Camera3DComponent& camera, const Vec3 cameraPosition, const Vec3 cameraRotation, const int width, const int height) {
  ProfileScope scope("Renderer3D.begin_frame");
  camera_ = camera;
  cameraPosition_ = cameraPosition;
  cameraRotation_ = cameraRotation;
  width_ = std::max(width, 1);
  height_ = std::max(height, 1);

  BeginDrawing();
  ClearBackground(toRlColor(camera.clearColor));

  const ::Vector3 position = toRlVec3(cameraPosition);
  const Vec3 rotatedTargetOffset = rotatePitchYaw(camera.targetOffset, cameraRotation);
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
  ProfileScope drawWorldScope("Renderer3D.draw_world");
  if (hasAlphaCutoutShader_) {
    const float viewPos[3] = {cameraPosition_.x, cameraPosition_.y, cameraPosition_.z};
    const float fogColor[4] = {camera_.clearColor.r, camera_.clearColor.g, camera_.clearColor.b, camera_.clearColor.a};
    constexpr float FogStart = 160.0F;
    constexpr float FogEnd = 224.0F;
    SetShaderValue(alphaCutoutShader_, GetShaderLocation(alphaCutoutShader_, "viewPos"), viewPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(alphaCutoutShader_, GetShaderLocation(alphaCutoutShader_, "fogColor"), fogColor, SHADER_UNIFORM_VEC4);
    SetShaderValue(alphaCutoutShader_, GetShaderLocation(alphaCutoutShader_, "fogStart"), &FogStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(alphaCutoutShader_, GetShaderLocation(alphaCutoutShader_, "fogEnd"), &FogEnd, SHADER_UNIFORM_FLOAT);
  }

  for (const Entity& entity : world.entities) {
    if (entity.directionalLight.has_value()) {
      const ::Vector3 direction = toRlVec3(entity.directionalLight->direction);
      const ::Color color = toRlColor(entity.directionalLight->color);
      DrawLine3D({0, 0, 0}, direction, color);
    }
  }

  for (const Entity& entity : world.entities) {
    if (entity.meshRenderer.has_value() || entity.boxCollider3D.has_value() || entity.sphereCollider3D.has_value()) {
      if (entity.meshRenderer.has_value() &&
          ![&]() {
            ProfileScope scope("Renderer3D.frustum_cull");
            return meshEntityVisible(
                world, entity, *entity.meshRenderer, cameraPosition_, cameraRotation_, camera_, static_cast<float>(width_) / static_cast<float>(height_));
          }()) {
          continue;
      }
      drawMeshEntity(world, entity, textures_, models_, dynamicModels_, hasAlphaCutoutShader_ ? &alphaCutoutShader_ : nullptr);
    }
  }

  std::unordered_set<std::string> liveDynamicModelIds;
  for (const Entity& entity : world.entities) {
    if (entity.meshRenderer.has_value() && !entity.meshRenderer->vertices.empty()) {
      liveDynamicModelIds.insert(entity.id);
    }
  }
  for (auto iterator = dynamicModels_.begin(); iterator != dynamicModels_.end();) {
    if (liveDynamicModelIds.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    if (iterator->second.hasModel) {
      UnloadModel(iterator->second.model);
    }
    iterator = dynamicModels_.erase(iterator);
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
  ProfileScope scope("Renderer3D.draw_hud");
  EndMode3D();

  const float canvasWidth = std::max(world.hudCanvasSize.x, 1.0F);
  const float canvasHeight = std::max(world.hudCanvasSize.y, 1.0F);
  const float scaleX = static_cast<float>(width_) / canvasWidth;
  const float scaleY = static_cast<float>(height_) / canvasHeight;
  const float textScale = std::min(scaleX, scaleY);
  constexpr float HudFontBaseSize = 8.0F;
  constexpr float HudFontMinSize = 4.0F;
  constexpr float HudLetterSpacing = 5.0F;

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

  for (const HudImageElement& element : world.hudImages) {
    if (!element.visible || element.texture.empty()) {
      continue;
    }
    const auto texture = textures_.find(element.texture);
    if (texture == textures_.end()) {
      continue;
    }
    const float sourceWidth = element.sourceSize.x != 0.0F ? element.sourceSize.x : static_cast<float>(texture->second.width);
    const float sourceHeight = element.sourceSize.y != 0.0F ? element.sourceSize.y : static_cast<float>(texture->second.height);
    const ::Rectangle source{
      .x = element.sourcePosition.x,
      .y = element.sourcePosition.y,
      .width = sourceWidth,
      .height = sourceHeight,
    };
    const ::Rectangle dest{
      .x = element.position.x * scaleX,
      .y = element.position.y * scaleY,
      .width = element.size.x * scaleX,
      .height = element.size.y * scaleY,
    };
    DrawTexturePro(texture->second, source, dest, ::Vector2{0.0F, 0.0F}, 0.0F, toRlColor(element.color));
  }

  for (const HudTextElement& element : world.hudText) {
    if (!element.visible) {
      continue;
    }
    const float authoredFontSize = element.fontSize > 0.0F ? element.fontSize : element.scale * HudFontBaseSize;
    const float fontSize = std::max(authoredFontSize * textScale, HudFontMinSize);
    DrawTextEx(GetFontDefault(),
               element.text.c_str(),
               Vector2{element.position.x * scaleX, element.position.y * scaleY},
               fontSize,
               HudLetterSpacing,
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
    const float authoredFontSize = element.fontSize > 0.0F ? element.fontSize : element.scale * HudFontBaseSize;
    const float fontSize = std::max(authoredFontSize * textScale, HudFontMinSize);
    const ::Vector2 measured = MeasureTextEx(GetFontDefault(), element.label.c_str(), fontSize, HudLetterSpacing);
    DrawTextEx(GetFontDefault(),
               element.label.c_str(),
               Vector2{rect.x + rect.width * 0.5F - measured.x * 0.5F, rect.y + rect.height * 0.5F - measured.y * 0.5F},
               fontSize,
               HudLetterSpacing,
               toRlColor(element.textColor));
  }
}

void Renderer3D::endFrame() {
  ProfileScope scope("Renderer3D.end_frame");
  EndDrawing();
}

} // namespace demi::runtime
