#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/Renderer3DInternal.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string_view>
#include <vector>

namespace demi::runtime::renderer3d_detail {
template <typename T> std::size_t bytesHash(const std::vector<T> &values) {
  const auto *bytes = reinterpret_cast<const char *>(values.data());
  return std::hash<std::string_view>{}(
      std::string_view{bytes, values.size() * sizeof(T)});
}

std::string dynamicMeshSignature(const MeshRendererComponent &mesh,
                                 const bool alphaCutout) {
  if (mesh.vertices.empty()) {
    return {};
  }
  if (mesh.revision > 0) {
    return "revision:" + std::to_string(mesh.revision) + ":" + mesh.texture +
           ":cutout:" + std::to_string(alphaCutout);
  }
  return std::to_string(mesh.vertices.size()) + ":" +
         std::to_string(bytesHash(mesh.vertices)) + ":" +
         std::to_string(mesh.normals.size()) + ":" +
         std::to_string(bytesHash(mesh.normals)) + ":" +
         std::to_string(mesh.uvs.size()) + ":" +
         std::to_string(bytesHash(mesh.uvs)) + ":" + mesh.texture +
         ":cutout:" + std::to_string(alphaCutout);
}

Mesh uploadDynamicMesh(const MeshRendererComponent &source) {
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
  RuntimeProfiler::addBytes("Renderer3D.dynamic_mesh_cpu_pack",
                            vertexBytes + normalBytes + uvBytes);

  {
    ProfileScope scope("Renderer3D.dynamic_mesh_cpu_pack");
    mesh.vertices =
        static_cast<float *>(MemAlloc(static_cast<unsigned int>(vertexBytes)));
    mesh.normals =
        static_cast<float *>(MemAlloc(static_cast<unsigned int>(normalBytes)));
    mesh.texcoords =
        static_cast<float *>(MemAlloc(static_cast<unsigned int>(uvBytes)));

    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
      const Vec3 &vertex = source.vertices[i];
      const Vec3 normal = i < source.normals.size() ? source.normals[i]
                                                    : Vec3{0.0F, 1.0F, 0.0F};
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

Model *dynamicModelForEntity(
    const Entity &entity, const MeshRendererComponent &mesh,
    const std::unordered_map<std::string, Texture2D> &textures,
    std::unordered_map<std::string, DynamicModelCacheEntry> &dynamicModels,
    const Shader *alphaCutoutShader) {
  if (mesh.vertices.empty()) {
    return nullptr;
  }
  auto cached = dynamicModels.find(entity.id);
  const bool useAlphaCutout =
      alphaCutoutShader != nullptr && !mesh.texture.empty();
  if (mesh.revision > 0 && cached != dynamicModels.end() &&
      cached->second.revision == mesh.revision &&
      cached->second.texture == mesh.texture && cached->second.hasModel &&
      cached->second.signature == dynamicMeshSignature(mesh, useAlphaCutout)) {
    return &cached->second.model;
  }
  const std::string signature = dynamicMeshSignature(mesh, useAlphaCutout);
  if (signature.empty()) {
    return nullptr;
  }
  if (cached != dynamicModels.end() && cached->second.signature == signature &&
      cached->second.hasModel) {
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
      SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE,
                         texture->second);
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

Model *animatedModelForEntity(
    const Entity &entity, const MeshRendererComponent &mesh,
    const std::unordered_map<std::string, std::filesystem::path> &modelPaths,
    const std::unordered_map<std::string, Texture2D> &modelTextures,
    std::unordered_map<std::string, AnimatedModelCacheEntry> &animatedModels) {
  if (mesh.model.empty()) {
    return nullptr;
  }
  const auto source = modelPaths.find(mesh.model);
  if (source == modelPaths.end()) {
    return nullptr;
  }

  auto cached = animatedModels.find(entity.id);
  if (cached != animatedModels.end() && cached->second.assetId == mesh.model &&
      cached->second.hasModel) {
    return &cached->second.model;
  }
  if (cached != animatedModels.end() && cached->second.hasModel) {
    UnloadModel(cached->second.model);
  }

  Model model = LoadModel(source->second.string().c_str());
  if (model.meshCount <= 0) {
    std::cerr << "Animated model load failed for " << mesh.model << " from "
              << source->second.string() << ".\n";
    return nullptr;
  }
  const auto texture = modelTextures.find(mesh.model);
  if (texture != modelTextures.end() && model.materialCount > 0) {
    SetMaterialTexture(&model.materials[0], MATERIAL_MAP_DIFFUSE,
                       texture->second);
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
      model.meshMaterial[meshIndex] = 0;
    }
  }
  cached = animatedModels
               .insert_or_assign(entity.id,
                                 AnimatedModelCacheEntry{.assetId = mesh.model,
                                                         .model = model,
                                                         .hasModel = true})
               .first;
  return &cached->second.model;
}

void updateModelAnimation(Model &model, AnimationPlayer3DComponent &player,
                          const ModelAnimationAsset &animations) {
  if (animations.clips == nullptr || animations.clipCount <= 0) {
    return;
  }
  const int clip = std::clamp(player.clip, 0, animations.clipCount - 1);
  const ModelAnimation &animation = animations.clips[clip];
  if (animation.frameCount <= 0) {
    return;
  }

  constexpr float GlTfAnimationFramesPerSecond = 60.0F;
  const float framePosition = player.time * GlTfAnimationFramesPerSecond;
  int frame = static_cast<int>(std::floor(framePosition));
  if (player.loop) {
    frame %= animation.frameCount;
  } else {
    frame = std::min(frame, animation.frameCount - 1);
  }
  UpdateModelAnimation(model, animation, std::max(frame, 0));
}

} // namespace demi::runtime::renderer3d_detail
