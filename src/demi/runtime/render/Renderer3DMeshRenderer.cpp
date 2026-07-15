#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/Renderer3DInternal.h"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <string_view>

namespace demi::runtime::renderer3d_detail {
::Color toRlColor(const Color &value) {
  return ::Color{
      static_cast<unsigned char>(
          std::round(std::clamp(value.r, 0.0F, 1.0F) * 255.0F)),
      static_cast<unsigned char>(
          std::round(std::clamp(value.g, 0.0F, 1.0F) * 255.0F)),
      static_cast<unsigned char>(
          std::round(std::clamp(value.b, 0.0F, 1.0F) * 255.0F)),
      static_cast<unsigned char>(
          std::round(std::clamp(value.a, 0.0F, 1.0F) * 255.0F)),
  };
}

::Vector3 toRlVec3(const Vec3 &value) {
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

float length(const Vec3 value) { return std::sqrt(dot(value, value)); }

Vec3 normalize(const Vec3 value) {
  const float valueLength = length(value);
  if (valueLength <= 0.0001F) {
    return Vec3{0.0F, 0.0F, -1.0F};
  }
  return scale(value, 1.0F / valueLength);
}

float horizontalRadiusForBounds(const Vec3 min, const Vec3 max,
                                const Vec3 scaleValue) {
  const Vec3 extents = multiply(scale(subtract(max, min), 0.5F), scaleValue);
  return length(
      Vec3{std::abs(extents.x), std::abs(extents.y), std::abs(extents.z)});
}
void drawTexturedPlane(const ::Vector3 center, const ::Vector2 size,
                       const Texture2D &texture, const ::Color tint) {
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

void drawTexturedCube(const ::Vector3 center, const ::Vector3 size,
                      const Texture2D &texture, const ::Color tint) {
  const float x0 = center.x - size.x * 0.5F;
  const float x1 = center.x + size.x * 0.5F;
  const float y0 = center.y - size.y * 0.5F;
  const float y1 = center.y + size.y * 0.5F;
  const float z0 = center.z - size.z * 0.5F;
  const float z1 = center.z + size.z * 0.5F;

  const auto face = [&](const ::Vector3 normal, const ::Vector3 a,
                        const ::Vector3 b, const ::Vector3 c,
                        const ::Vector3 d) {
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
  face({0.0F, 0.0F, 1.0F}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1},
       {x0, y1, z1});
  face({0.0F, 0.0F, -1.0F}, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0},
       {x1, y1, z0});
  face({1.0F, 0.0F, 0.0F}, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0},
       {x1, y1, z1});
  face({-1.0F, 0.0F, 0.0F}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1},
       {x0, y1, z0});
  face({0.0F, 1.0F, 0.0F}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0},
       {x0, y1, z0});
  face({0.0F, -1.0F, 0.0F}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1},
       {x0, y0, z1});
  rlEnd();
  rlSetTexture(0);
}

bool meshEntityVisible(const World &world, const Entity &entity,
                       const MeshRendererComponent &mesh, const Vec3 camera,
                       const Vec3 cameraForward, const Vec3 cameraUp,
                       const Camera3DComponent &cameraComponent,
                       const float aspectRatio) {
  if (!entity.hasComponent<Transform3DComponent>() ||
      !cameraComponent.perspective) {
    return true;
  }

  const auto transform = resolveWorldTransform3D(world, entity);
  if (!transform)
    return false;
  const Vec3 position = transform->position;
  const Vec3 scaleValue = transform->scale;
  Vec3 center = position;
  float radius = length(scaleValue) * 0.5F;

  if (mesh.hasBounds) {
    const Vec3 localCenter =
        multiply(scale(add(mesh.boundsMin, mesh.boundsMax), 0.5F), mesh.size);
    center = transformPoint3D(*transform, localCenter);
    radius = horizontalRadiusForBounds(mesh.boundsMin, mesh.boundsMax,
                                       multiply(scaleValue, mesh.size));
  } else if (mesh.shape == "sphere") {
    const Vec3 scaledSize = multiply(mesh.size, scaleValue);
    radius = std::max({std::abs(scaledSize.x), std::abs(scaledSize.y),
                       std::abs(scaledSize.z)}) *
             0.5F;
  } else {
    radius = length(multiply(mesh.size, scaleValue)) * 0.5F;
  }

  const Vec3 forward = normalize(cameraForward);
  Vec3 right = normalize(cross(forward, normalize(cameraUp)));
  if (length(right) <= 0.0001F) {
    right = Vec3{1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(right, forward));
  const Vec3 toCenter = subtract(center, camera);
  const float distanceAlongForward = dot(toCenter, forward);
  if (distanceAlongForward + radius < cameraComponent.nearClip ||
      distanceAlongForward - radius > cameraComponent.farClip) {
    return false;
  }

  const float halfVerticalFov = (std::max(cameraComponent.fov, 1.0F) *
                                 std::numbers::pi_v<float> / 180.0F) *
                                0.5F;
  const float verticalLimit =
      std::max(distanceAlongForward, 0.0F) * std::tan(halfVerticalFov) + radius;
  const float horizontalLimit =
      verticalLimit * std::max(aspectRatio, 1.0F) + radius;
  return std::abs(dot(toCenter, up)) <= verticalLimit &&
         std::abs(dot(toCenter, right)) <= horizontalLimit;
}

void drawMeshEntity(
    const World &world, Entity &entity,
    const std::unordered_map<std::string, Texture2D> &textures,
    const std::unordered_map<std::string, Model> &models,
    const std::unordered_map<std::string, std::filesystem::path> &modelPaths,
    const std::unordered_map<std::string, Texture2D> &modelTextures,
    const std::unordered_map<std::string, TextureImporterSettings>
        &modelTextureSettings,
    const std::unordered_map<std::string, ModelAnimationAsset> &modelAnimations,
    std::unordered_map<std::string, AnimatedModelCacheEntry> &animatedModels,
    std::unordered_map<std::string, DynamicModelCacheEntry> &dynamicModels,
    const bool drawDebugColliders, const Shader *alphaCutoutShader) {
  if (!entity.hasComponent<MeshRendererComponent>() ||
      !entity.hasComponent<Transform3DComponent>()) {
    return;
  }

  const MeshRendererComponent &mesh =
      *entity.component<MeshRendererComponent>();
  const auto worldTransform = resolveWorldTransform3D(world, entity);
  if (!worldTransform)
    return;
  const ::Vector3 position = toRlVec3(worldTransform->position);
  const Vec3 scaledSize = multiply(mesh.size, worldTransform->scale);
  const ::Vector3 size = toRlVec3(scaledSize);
  const ::Color color = toRlColor(mesh.color);
  const bool drawSolid = color.a > 0 && !mesh.wireframe;

  const ::Vector3 rotation = toRlVec3(worldTransform->rotation);
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
  Model *dynamicModel = nullptr;
  const Model *staticModel = nullptr;
  if (!mesh.vertices.empty()) {
    ProfileScope scope("Renderer3D.dynamic_model_lookup");
    dynamicModel = dynamicModelForEntity(entity, mesh, textures, dynamicModels,
                                         alphaCutoutShader);
  } else if (!mesh.model.empty()) {
    if (entity.hasComponent<AnimationPlayer3DComponent>()) {
      const auto animations = modelAnimations.find(mesh.model);
      if (animations != modelAnimations.end()) {
        if (Model *animatedModel =
                animatedModelForEntity(entity, mesh, modelPaths, modelTextures,
                                       modelTextureSettings, animatedModels)) {
          updateModelAnimation(*animatedModel,
                               *entity.component<AnimationPlayer3DComponent>(),
                               animations->second);
          staticModel = animatedModel;
        }
      }
    }
    if (staticModel == nullptr) {
      const auto found = models.find(mesh.model);
      if (found != models.end()) {
        staticModel = &found->second;
      }
    }
  }

  const auto drawShape = [&](const Texture2D &tex, bool textured) {
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
        if (alphaCutoutShader != nullptr && !mesh.texture.empty() &&
            dynamicModel->materialCount > 0) {
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
      DrawCylinderWires(position, size.x * 0.5F, size.x * 0.5F, size.y, 16,
                        color);
    } else {
      DrawCubeWiresV(position, size, color);
    }
    rlPopMatrix();
  }

  if (drawDebugColliders) {
    if (const auto triangles = resolvedTriangleCollider3D(world, entity)) {
      for (const TriangleCollider3D &triangle : *triangles) {
        const ::Vector3 first =
            toRlVec3(transformPoint3D(*worldTransform, triangle.a));
        const ::Vector3 second =
            toRlVec3(transformPoint3D(*worldTransform, triangle.b));
        const ::Vector3 third =
            toRlVec3(transformPoint3D(*worldTransform, triangle.c));
        DrawLine3D(first, second, {244, 91, 105, 255});
        DrawLine3D(second, third, {244, 91, 105, 255});
        DrawLine3D(third, first, {244, 91, 105, 255});
      }
    } else if (const auto collider = resolvedBoxCollider3D(world, entity)) {
      const ::Vector3 colliderCenter =
          toRlVec3(transformPoint3D(*worldTransform, collider->offset));
      const ::Vector3 colliderSize =
          toRlVec3(multiply(collider->size, worldTransform->scale));
      rlPushMatrix();
      rlTranslatef(colliderCenter.x, colliderCenter.y, colliderCenter.z);
      rlRotatef(rotationX, 1.0F, 0.0F, 0.0F);
      rlRotatef(rotationY, 0.0F, 1.0F, 0.0F);
      rlRotatef(rotationZ, 0.0F, 0.0F, 1.0F);
      rlTranslatef(-colliderCenter.x, -colliderCenter.y, -colliderCenter.z);
      DrawCubeWiresV(colliderCenter, colliderSize, {244, 91, 105, 255});
      rlPopMatrix();
    }
  }
  if (drawDebugColliders && entity.hasComponent<SphereCollider3DComponent>()) {
    const auto &collider = *entity.component<SphereCollider3DComponent>();
    const ::Vector3 colliderCenter =
        toRlVec3(transformPoint3D(*worldTransform, collider.offset));
    const float maximumScale = std::max({std::abs(worldTransform->scale.x),
                                         std::abs(worldTransform->scale.y),
                                         std::abs(worldTransform->scale.z)});
    DrawSphereWires(colliderCenter, collider.radius * maximumScale, 16, 16,
                    {244, 91, 105, 255});
  }
}

} // namespace demi::runtime::renderer3d_detail
