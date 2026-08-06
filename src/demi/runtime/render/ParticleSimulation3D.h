#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct World;

struct ParticleRenderData3D {
  Vec3 position;
  Color color;
  float size = 0.0F;
  float rotation = 0.0F;
  std::string texture;
  std::string material;
  int sortingOrder = 0;
};

// Owns deterministic particle state without depending on a graphics API.
// Presenters consume the compact render-data snapshot without owning the
// deterministic simulation state.
class ParticleSimulation3D {
public:
  void update(const World &world, float deltaTime);
  [[nodiscard]] std::vector<ParticleRenderData3D>
  renderData(std::string_view renderMask) const;
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
