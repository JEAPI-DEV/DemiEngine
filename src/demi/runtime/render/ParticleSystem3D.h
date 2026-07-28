#pragma once

#include "demi/assets/RenderAsset.h"
#include "demi/runtime/render/RenderStatistics.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct World;

class ParticleSystem3D {
public:
  void update(const World &world, float deltaTime);
  void draw(
      const World &world, const ::Camera3D &camera,
      const std::unordered_map<std::string, Texture2D> &textures,
      const std::unordered_map<std::string, assets::MaterialAsset> &materials,
      const std::string &renderMask, RenderStatistics &statistics);
  void clear();
  [[nodiscard]] std::size_t particleCount() const;

private:
  struct Particle {
    Vec3 position;
    Vec3 velocity;
    Vec3 gravity;
    Color colorStart;
    Color colorEnd;
    float age = 0.0F;
    float lifetime = 1.0F;
    float sizeStart = 0.1F;
    float sizeEnd = 0.0F;
    float rotation = 0.0F;
    float rotationSpeed = 0.0F;
  };

  struct EmitterState {
    std::vector<Particle> particles;
    std::uint32_t randomState = 1;
    float emissionRemainder = 0.0F;
    bool burstEmitted = false;
    std::string renderMask;
    std::string texture;
    std::string material;
    std::string simulationSpace = "world";
    Vec3 origin;
    int sortingOrder = 0;
  };

  std::unordered_map<std::string, EmitterState> emitters_;
};

} // namespace demi::runtime
