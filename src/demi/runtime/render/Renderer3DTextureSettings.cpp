#include "demi/runtime/render/Renderer3DInternal.h"

#include "demi/runtime/render/TextureSamplerSettings.h"

#include <rlgl.h>

#include <unordered_set>

namespace demi::runtime::renderer3d_detail {

void applyTextureSettings(Texture2D &texture,
                          const TextureImporterSettings &settings,
                          const int defaultFilter) {
  render_detail::applyTextureSamplerSettings(texture, settings, defaultFilter);
}

void applyModelTextureSettings(Model &model,
                               const TextureImporterSettings &settings) {
  for (int material = 0; material < model.materialCount; ++material)
    for (int map = 0; map <= MATERIAL_MAP_BRDF; ++map) {
      Texture2D &texture = model.materials[material].maps[map].texture;
      if (texture.id != 0)
        applyTextureSettings(texture, settings, TEXTURE_FILTER_BILINEAR);
    }
}

std::vector<Texture2D> ownedTexturesForModel(const Model &model,
                                             const unsigned int excludedId) {
  std::vector<Texture2D> textures;
  std::unordered_set<unsigned int> ids;
  ids.insert(0);
  ids.insert(rlGetTextureIdDefault());
  ids.insert(excludedId);
  for (int material = 0; material < model.materialCount; ++material)
    for (int map = 0; map <= MATERIAL_MAP_BRDF; ++map) {
      const Texture2D texture = model.materials[material].maps[map].texture;
      if (ids.insert(texture.id).second)
        textures.push_back(texture);
    }
  return textures;
}

void unloadOwnedTextures(std::vector<Texture2D> &textures) {
  for (const Texture2D texture : textures)
    UnloadTexture(texture);
  textures.clear();
}

} // namespace demi::runtime::renderer3d_detail
