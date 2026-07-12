#pragma once

#include "demi/runtime/render/Renderer3D.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace demi::runtime::renderer3d_detail {

[[nodiscard]] ::Color toRlColor(const Color &value);
[[nodiscard]] ::Vector3 toRlVec3(const Vec3 &value);
[[nodiscard]] Vec3 rotatePitchYaw(Vec3 value, Vec3 rotation);

[[nodiscard]] bool meshEntityVisible(const World &world, const Entity &entity,
                                     const MeshRendererComponent &mesh,
                                     Vec3 camera, Vec3 cameraRotation,
                                     const Camera3DComponent &cameraComponent,
                                     float aspectRatio);

[[nodiscard]] Model *dynamicModelForEntity(
    const Entity &entity, const MeshRendererComponent &mesh,
    const std::unordered_map<std::string, Texture2D> &textures,
    std::unordered_map<std::string, DynamicModelCacheEntry> &dynamicModels,
    const Shader *alphaCutoutShader);

[[nodiscard]] Model *animatedModelForEntity(
    const Entity &entity, const MeshRendererComponent &mesh,
    const std::unordered_map<std::string, std::filesystem::path> &modelPaths,
    const std::unordered_map<std::string, Texture2D> &modelTextures,
    std::unordered_map<std::string, AnimatedModelCacheEntry> &animatedModels);

void updateModelAnimation(Model &model, AnimationPlayer3DComponent &player,
                          const ModelAnimationAsset &animations);

void drawMeshEntity(
    const World &world, Entity &entity,
    const std::unordered_map<std::string, Texture2D> &textures,
    const std::unordered_map<std::string, Model> &models,
    const std::unordered_map<std::string, std::filesystem::path> &modelPaths,
    const std::unordered_map<std::string, Texture2D> &modelTextures,
    const std::unordered_map<std::string, ModelAnimationAsset> &modelAnimations,
    std::unordered_map<std::string, AnimatedModelCacheEntry> &animatedModels,
    std::unordered_map<std::string, DynamicModelCacheEntry> &dynamicModels,
    const Shader *alphaCutoutShader);

} // namespace demi::runtime::renderer3d_detail
