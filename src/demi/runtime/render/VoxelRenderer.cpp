#include "demi/runtime/render/VoxelRenderer.h"

#include "demi/runtime/voxel/VoxelChunk.h"
#include "demi/runtime/voxel/VoxelMesher.h"
#include "demi/runtime/voxel/VoxelTerrain.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace demi::runtime {

namespace {

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
      image.pixels.push_back(scale(std::stoi(*rText)));
      image.pixels.push_back(scale(std::stoi(*gText)));
      image.pixels.push_back(scale(std::stoi(*bText)));
      image.pixels.push_back(255U);
    }
  } catch (...) {
    return std::nullopt;
  }
  return image;
}

Texture2D loadAtlasTexture(const std::filesystem::path& path) {
  if (path.extension() != ".ppm") {
    return LoadTexture(path.string().c_str());
  }
  const std::optional<ImageData> image = loadPpm(path);
  if (!image.has_value()) {
    return {};
  }
  ::Image rlImage{
    .data = const_cast<void*>(static_cast<const void*>(image->pixels.data())),
    .width = image->width,
    .height = image->height,
    .mipmaps = 1,
    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
  };
  return LoadTextureFromImage(rlImage);
}

unsigned char tintChannel(const float value) {
  return static_cast<unsigned char>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
}

Texture2D composeAtlasTexture(const std::vector<VoxelAtlasSource>& sources, const int tileSize) {
  if (sources.empty() || tileSize <= 0) {
    return {};
  }

  Image atlas = GenImageColor(tileSize * static_cast<int>(sources.size()), tileSize, ::Color{0, 0, 0, 0});
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const VoxelAtlasSource& source = sources[index];
    Image tile = LoadImage(source.path.string().c_str());
    if (tile.data == nullptr) {
      std::cerr << "Voxel atlas tile load failed: " << source.path.string() << '\n';
      continue;
    }
    if (tile.width != tileSize || tile.height != tileSize) {
      ImageResizeNN(&tile, tileSize, tileSize);
    }
    ImageColorTint(&tile, ::Color{tintChannel(source.tintR), tintChannel(source.tintG), tintChannel(source.tintB), tintChannel(source.tintA)});
    ImageDraw(&atlas,
              tile,
              Rectangle{.x = 0.0F, .y = 0.0F, .width = static_cast<float>(tileSize), .height = static_cast<float>(tileSize)},
              Rectangle{.x = static_cast<float>(index * static_cast<std::size_t>(tileSize)), .y = 0.0F, .width = static_cast<float>(tileSize), .height = static_cast<float>(tileSize)},
              ::Color{255, 255, 255, 255});
    UnloadImage(tile);
  }

  Texture2D texture = LoadTextureFromImage(atlas);
  UnloadImage(atlas);
  return texture;
}

std::string chunkSignature(const Entity& entity) {
  if (!entity.voxelChunk.has_value()) {
    return {};
  }
  const VoxelChunkComponent& chunk = *entity.voxelChunk;
  std::ostringstream signature;
  signature << chunk.blockSet << ':' << chunk.width << 'x' << chunk.height << 'x' << chunk.depth << ':';
  for (const VoxelLayer& layer : chunk.layers) {
    signature << layer.block << '[' << layer.fromY << ',' << layer.toY << ']';
  }
  if (chunk.terrain.enabled) {
    signature << ":terrain{" << chunk.terrain.seed << ',' << chunk.terrain.baseHeight << ',' << chunk.terrain.amplitude << ','
              << chunk.terrain.frequency << ',' << chunk.terrain.dirtDepth << ',' << chunk.terrain.surfaceBlock << ','
              << chunk.terrain.subsurfaceBlock << ',' << chunk.terrain.stoneBlock;
    if (entity.transform3D.has_value()) {
      signature << '@' << static_cast<int>(std::floor(entity.transform3D->position.x)) << ',' << static_cast<int>(std::floor(entity.transform3D->position.z));
    }
    signature << '}';
  }
  return signature.str();
}

VoxelChunk buildChunkFromComponent(const Entity& entity, const VoxelBlockSet& blockSet) {
  const VoxelChunkComponent& component = *entity.voxelChunk;
  VoxelChunk chunk(std::max(component.width, 1), std::max(component.height, 1), std::max(component.depth, 1));
  if (component.terrain.enabled) {
    const Vec3 origin = entity.transform3D.has_value() ? entity.transform3D->position : Vec3{};
    generateVoxelTerrain(chunk, blockSet, component.terrain, static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.z)));
    return chunk;
  }
  for (const VoxelLayer& layer : component.layers) {
    const std::uint16_t blockId = blockSet.blockIdByName(layer.block);
    const int fromY = std::clamp(std::min(layer.fromY, layer.toY), 0, chunk.height() - 1);
    const int toY = std::clamp(std::max(layer.fromY, layer.toY), 0, chunk.height() - 1);
    for (int y = fromY; y <= toY; ++y) {
      for (int z = 0; z < chunk.depth(); ++z) {
        for (int x = 0; x < chunk.width(); ++x) {
          chunk.setBlock(x, y, z, blockId);
        }
      }
    }
  }
  return chunk;
}

Mesh uploadVoxelMesh(const VoxelMeshData& meshData) {
  Mesh mesh{};
  mesh.vertexCount = static_cast<int>(meshData.positions.size());
  mesh.triangleCount = mesh.vertexCount / 3;
  if (mesh.vertexCount == 0) {
    return mesh;
  }

  mesh.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(meshData.positions.size() * 3 * sizeof(float))));
  mesh.normals = static_cast<float*>(MemAlloc(static_cast<unsigned int>(meshData.normals.size() * 3 * sizeof(float))));
  mesh.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned int>(meshData.texcoords.size() * 2 * sizeof(float))));

  for (std::size_t i = 0; i < meshData.positions.size(); ++i) {
    mesh.vertices[i * 3 + 0] = meshData.positions[i].x;
    mesh.vertices[i * 3 + 1] = meshData.positions[i].y;
    mesh.vertices[i * 3 + 2] = meshData.positions[i].z;
    mesh.normals[i * 3 + 0] = meshData.normals[i].x;
    mesh.normals[i * 3 + 1] = meshData.normals[i].y;
    mesh.normals[i * 3 + 2] = meshData.normals[i].z;
    mesh.texcoords[i * 2 + 0] = meshData.texcoords[i].x;
    mesh.texcoords[i * 2 + 1] = meshData.texcoords[i].y;
  }

  UploadMesh(&mesh, false);
  return mesh;
}

} // namespace

VoxelRenderer::~VoxelRenderer() {
  for (auto& [id, chunk] : chunkMeshes_) {
    (void)id;
    if (chunk.hasModel) {
      UnloadModel(chunk.model);
    }
  }
  for (auto& [id, blockSet] : blockSets_) {
    (void)id;
    if (blockSet.atlas.id != 0) {
      UnloadTexture(blockSet.atlas);
    }
  }
}

void VoxelRenderer::loadAssets(const AssetRegistry& registry) {
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type != "VoxelBlockSet") {
      continue;
    }
    std::string error;
    std::optional<VoxelBlockSet> blockSet = loadVoxelBlockSet(asset.sourcePath, asset.id, error);
    if (!blockSet.has_value()) {
      std::cerr << error << '\n';
      continue;
    }

    Texture2D atlas = asset.atlasPath.has_value() ? loadAtlasTexture(*asset.atlasPath) : composeAtlasTexture(blockSet->atlasSources, blockSet->tileSize);
    if (atlas.id == 0) {
      std::cerr << "Voxel atlas load failed for " << asset.id << ".\n";
      continue;
    }
    SetTextureFilter(atlas, TEXTURE_FILTER_POINT);
    const int tileSize = std::max(blockSet->tileSize, 1);
    blockSets_[asset.id] = LoadedBlockSet{
      .blockSet = std::move(*blockSet),
      .atlas = atlas,
      .atlasColumns = std::max(atlas.width / tileSize, 1),
      .atlasRows = std::max(atlas.height / tileSize, 1),
    };
  }
}

VoxelRenderer::CachedChunkMesh VoxelRenderer::buildChunkMesh(const Entity& entity, const LoadedBlockSet& blockSet) const {
  CachedChunkMesh cached;
  cached.signature = chunkSignature(entity);
  const VoxelChunk chunk = buildChunkFromComponent(entity, blockSet.blockSet);
  const VoxelMeshData meshData = buildVoxelMesh(chunk, blockSet.blockSet, VoxelMeshBuildOptions{.atlasColumns = blockSet.atlasColumns, .atlasRows = blockSet.atlasRows});
  cached.faceCount = meshData.faceCount;
  if (meshData.positions.empty()) {
    return cached;
  }

  Mesh mesh = uploadVoxelMesh(meshData);
  cached.model = LoadModelFromMesh(mesh);
  if (cached.model.materialCount > 0) {
    SetMaterialTexture(&cached.model.materials[0], MATERIAL_MAP_DIFFUSE, blockSet.atlas);
  }
  cached.hasModel = true;
  return cached;
}

void VoxelRenderer::drawWorld(const World& world) {
  for (const Entity& entity : world.entities) {
    if (!entity.voxelChunk.has_value() || !entity.transform3D.has_value()) {
      continue;
    }

    const auto blockSet = blockSets_.find(entity.voxelChunk->blockSet);
    if (blockSet == blockSets_.end()) {
      continue;
    }

    const std::string signature = chunkSignature(entity);
    auto cached = chunkMeshes_.find(entity.id);
    if (cached == chunkMeshes_.end() || cached->second.signature != signature) {
      if (cached != chunkMeshes_.end() && cached->second.hasModel) {
        UnloadModel(cached->second.model);
      }
      cached = chunkMeshes_.insert_or_assign(entity.id, buildChunkMesh(entity, blockSet->second)).first;
      std::cerr << "Voxel debug: built " << entity.id << " with " << cached->second.faceCount << " visible face(s).\n";
    }

    if (!cached->second.hasModel) {
      continue;
    }

    const Vec3 position = worldPosition3D(world, entity);
    const Vec3 rotation = worldRotation3D(world, entity);
    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(rotation.x * (180.0F / 3.14159265358979F), 1.0F, 0.0F, 0.0F);
    rlRotatef(rotation.y * (180.0F / 3.14159265358979F), 0.0F, 1.0F, 0.0F);
    rlRotatef(rotation.z * (180.0F / 3.14159265358979F), 0.0F, 0.0F, 1.0F);
    DrawModel(cached->second.model, {0.0F, 0.0F, 0.0F}, 1.0F, ::Color{255, 255, 255, 255});
    rlPopMatrix();

    if (entity.voxelChunk->debug) {
      const Vector3 size{static_cast<float>(entity.voxelChunk->width), static_cast<float>(entity.voxelChunk->height), static_cast<float>(entity.voxelChunk->depth)};
      const Vector3 center{position.x + size.x * 0.5F, position.y + size.y * 0.5F, position.z + size.z * 0.5F};
      DrawCubeWiresV(center, size, {245, 245, 245, 255});
    }
  }
}

} // namespace demi::runtime
