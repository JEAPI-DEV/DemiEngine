#pragma once

#include "demi/assets/RenderAsset.h"
#include "demi/runtime/render/bgfx2d/ParticleRenderData2D.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/render/ShaderResourceLibrary.h"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct World;

class ParticleSystem2D {
public:
  void update(const World &world, float deltaTime);
  void draw(const std::unordered_map<std::string, Texture2D> &textures,
            const std::unordered_map<std::string, assets::MaterialAsset>
                &materials,
            const ShaderResourceLibrary &shaders,
            Vec2 cameraPosition, float pixelsPerUnit, int width, int height);
  void clear();
  [[nodiscard]] std::size_t particleCount() const;
  [[nodiscard]] std::vector<render::ParticleRenderData2D> renderData() const;

private:
  struct Particle {
    Vec2 position;
    Vec2 velocity;
    Vec2 gravity;
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
    std::string texture;
    std::string material;
    int sortingOrder = 0;
  };

  std::unordered_map<std::string, EmitterState> emitters_;
};

} // namespace demi::runtime
