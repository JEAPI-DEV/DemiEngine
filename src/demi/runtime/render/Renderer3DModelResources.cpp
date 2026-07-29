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
      cached->second.texture == mesh.texture && cached->second.hasModel) {
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
    const std::unordered_map<std::string, TextureImporterSettings>
        &textureSettings,
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
    unloadOwnedTextures(cached->second.ownedTextures);
  }

  Model model = LoadModel(source->second.string().c_str());
  if (model.meshCount <= 0) {
    std::cerr << "Animated model load failed for " << mesh.model << " from "
              << source->second.string() << ".\n";
    return nullptr;
  }
  if (const auto settings = textureSettings.find(mesh.model);
      settings != textureSettings.end())
    applyModelTextureSettings(model, settings->second);
  std::vector<Texture2D> ownedTextures = ownedTexturesForModel(model);
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
                                 AnimatedModelCacheEntry{
                                     .assetId = mesh.model,
                                     .model = model,
                                     .ownedTextures = std::move(ownedTextures),
                                     .hasModel = true})
               .first;
  return &cached->second.model;
}

void updateModelAnimation(Model &model, AnimationPlayer3DComponent &player,
                          const ModelAnimationAsset &animations) {
  if (animations.clips == nullptr || animations.clipCount <= 0) {
    return;
  }
  const auto resolveClip = [&](const int requested,
                               const std::string &name) {
    int result = requested;
    if (!name.empty())
      if (const auto named = animations.clipsByName.find(name);
          named != animations.clipsByName.end())
        result = named->second;
    return std::clamp(result, 0, animations.clipCount - 1);
  };
  const auto frameFor = [](const ModelAnimation &animation, const float time,
                           const bool loop) {
    constexpr float GlTfAnimationFramesPerSecond = 60.0F;
    int frame = static_cast<int>(
        std::floor(time * GlTfAnimationFramesPerSecond));
    if (loop)
      frame %= animation.frameCount;
    else
      frame = std::min(frame, animation.frameCount - 1);
    return std::max(frame, 0);
  };
  const auto blendVector = [](const Vector3 left, const Vector3 right,
                              const float weight) {
    return Vector3{left.x + (right.x - left.x) * weight,
                   left.y + (right.y - left.y) * weight,
                   left.z + (right.z - left.z) * weight};
  };
  const auto normalizeQuaternion = [](Quaternion value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y +
                                   value.z * value.z + value.w * value.w);
    if (length <= 0.000001F)
      return Quaternion{0.0F, 0.0F, 0.0F, 1.0F};
    return Quaternion{value.x / length, value.y / length, value.z / length,
                      value.w / length};
  };
  const auto blendQuaternion = [&](Quaternion left, Quaternion right,
                                   const float weight) {
    const float dot = left.x * right.x + left.y * right.y +
                      left.z * right.z + left.w * right.w;
    if (dot < 0.0F)
      right = {-right.x, -right.y, -right.z, -right.w};
    return normalizeQuaternion(
        {left.x + (right.x - left.x) * weight,
         left.y + (right.y - left.y) * weight,
         left.z + (right.z - left.z) * weight,
         left.w + (right.w - left.w) * weight});
  };
  const auto multiplyQuaternion = [&](const Quaternion left,
                                      const Quaternion right) {
    return normalizeQuaternion(
        {left.w * right.x + left.x * right.w + left.y * right.z -
             left.z * right.y,
         left.w * right.y - left.x * right.z + left.y * right.w +
             left.z * right.x,
         left.w * right.z + left.x * right.y - left.y * right.x +
             left.z * right.w,
         left.w * right.w - left.x * right.x - left.y * right.y -
             left.z * right.z});
  };
  const auto inverseQuaternion = [](const Quaternion value) {
    return Quaternion{-value.x, -value.y, -value.z, value.w};
  };

  const int clip = resolveClip(player.clip, player.clipName);
  const ModelAnimation &animation = animations.clips[clip];
  if (animation.frameCount <= 0) {
    return;
  }

  const int frame = frameFor(animation, player.time, player.loop);
  if (animation.boneCount <= 0 || animation.framePoses == nullptr ||
      animation.framePoses[frame] == nullptr) {
    UpdateModelAnimation(model, animation, frame);
    return;
  }

  std::vector<Transform> pose(
      animation.framePoses[frame],
      animation.framePoses[frame] + animation.boneCount);
  if (player.blendWeight < 1.0F &&
      (player.previousClip >= 0 || !player.previousClipName.empty())) {
    const ModelAnimation &previous =
        animations.clips[resolveClip(player.previousClip,
                                     player.previousClipName)];
    if (previous.frameCount > 0 && previous.boneCount == animation.boneCount &&
        previous.framePoses != nullptr) {
      const int previousFrame =
          frameFor(previous, player.previousTime + player.time, true);
      for (int bone = 0; bone < animation.boneCount; ++bone) {
        const Transform &source = previous.framePoses[previousFrame][bone];
        pose[bone].translation =
            blendVector(source.translation, pose[bone].translation,
                        player.blendWeight);
        pose[bone].rotation =
            blendQuaternion(source.rotation, pose[bone].rotation,
                            player.blendWeight);
        pose[bone].scale =
            blendVector(source.scale, pose[bone].scale, player.blendWeight);
      }
    }
  }

  for (const AnimationLayerPlayback3D &layer : player.layers) {
    const ModelAnimation &layerAnimation =
        animations.clips[resolveClip(layer.clip, layer.clipName)];
    if (layerAnimation.frameCount <= 0 ||
        layerAnimation.boneCount != animation.boneCount ||
        layerAnimation.framePoses == nullptr)
      continue;
    const int layerFrame = frameFor(layerAnimation, player.time, true);
    for (int bone = 0; bone < animation.boneCount; ++bone) {
      const std::string boneName = animation.bones != nullptr
                                       ? animation.bones[bone].name
                                       : std::string{};
      if (!layer.mask.empty() &&
          std::ranges::find(layer.mask, boneName) == layer.mask.end())
        continue;
      const Transform &sample = layerAnimation.framePoses[layerFrame][bone];
      if (!layer.additive) {
        pose[bone].translation =
            blendVector(pose[bone].translation, sample.translation,
                        layer.weight);
        pose[bone].rotation =
            blendQuaternion(pose[bone].rotation, sample.rotation,
                            layer.weight);
        pose[bone].scale =
            blendVector(pose[bone].scale, sample.scale, layer.weight);
        continue;
      }
      const Transform bind = model.bindPose != nullptr
                                 ? model.bindPose[bone]
                                 : Transform{.translation = {},
                                             .rotation = {0, 0, 0, 1},
                                             .scale = {1, 1, 1}};
      pose[bone].translation.x +=
          (sample.translation.x - bind.translation.x) * layer.weight;
      pose[bone].translation.y +=
          (sample.translation.y - bind.translation.y) * layer.weight;
      pose[bone].translation.z +=
          (sample.translation.z - bind.translation.z) * layer.weight;
      pose[bone].scale.x += (sample.scale.x - bind.scale.x) * layer.weight;
      pose[bone].scale.y += (sample.scale.y - bind.scale.y) * layer.weight;
      pose[bone].scale.z += (sample.scale.z - bind.scale.z) * layer.weight;
      const Quaternion delta =
          multiplyQuaternion(sample.rotation,
                             inverseQuaternion(bind.rotation));
      pose[bone].rotation = multiplyQuaternion(
          pose[bone].rotation,
          blendQuaternion({0, 0, 0, 1}, delta, layer.weight));
    }
  }

  Transform *framePose = pose.data();
  ModelAnimation blended{.boneCount = animation.boneCount,
                         .frameCount = 1,
                         .bones = animation.bones,
                         .framePoses = &framePose,
                         .name = {}};
  UpdateModelAnimation(model, blended, 0);
}

} // namespace demi::runtime::renderer3d_detail
