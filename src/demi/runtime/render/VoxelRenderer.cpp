#include "demi/runtime/render/VoxelRenderer.h"

#include "demi/runtime/scene/VoxelChunkBuilder.h"
#include "demi/runtime/voxel/VoxelMesher.h"

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

struct ChunkOrigin {
  int x = 0;
  int y = 0;
  int z = 0;
};

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

std::string chunkSignature(const Entity& entity, const Vec3 worldOrigin) {
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
      signature << '@' << static_cast<int>(std::floor(worldOrigin.x)) << ',' << static_cast<int>(std::floor(worldOrigin.z));
    }
    signature << '}';
  }
  return signature.str();
}

ChunkOrigin chunkOrigin(const Vec3 worldOrigin) {
  return ChunkOrigin{
    .x = static_cast<int>(std::floor(worldOrigin.x)),
    .y = static_cast<int>(std::floor(worldOrigin.y)),
    .z = static_cast<int>(std::floor(worldOrigin.z)),
  };
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

struct VoxelRenderer::RenderChunk {
  const Entity* entity = nullptr;
  const LoadedBlockSet* blockSet = nullptr;
  VoxelChunk data;
  ChunkOrigin origin;
  std::string signature;
};

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

VoxelRenderer::CachedChunkMesh VoxelRenderer::buildChunkMesh(const RenderChunk& chunk, const std::vector<RenderChunk>& chunks) const {
  CachedChunkMesh cached;
  cached.signature = chunk.signature;
  const VoxelMeshData meshData = buildVoxelMesh(
    chunk.data,
    chunk.blockSet->blockSet,
    VoxelMeshBuildOptions{
      .atlasColumns = chunk.blockSet->atlasColumns,
      .atlasRows = chunk.blockSet->atlasRows,
      .isSolidNeighbor =
        [&chunk, &chunks](const int x, const int y, const int z) {
          const int worldX = chunk.origin.x + x;
          const int worldY = chunk.origin.y + y;
          const int worldZ = chunk.origin.z + z;
          for (const RenderChunk& neighbor : chunks) {
            const bool contains = worldX >= neighbor.origin.x && worldX < neighbor.origin.x + neighbor.data.width() && worldY >= neighbor.origin.y &&
                                  worldY < neighbor.origin.y + neighbor.data.height() && worldZ >= neighbor.origin.z && worldZ < neighbor.origin.z + neighbor.data.depth();
            if (neighbor.entity == chunk.entity || !contains) {
              continue;
            }
            return neighbor.blockSet->blockSet.isSolid(neighbor.data.block(worldX - neighbor.origin.x, worldY - neighbor.origin.y, worldZ - neighbor.origin.z));
          }
          return false;
        },
    });
  cached.faceCount = meshData.faceCount;
  if (meshData.positions.empty()) {
    return cached;
  }

  Mesh mesh = uploadVoxelMesh(meshData);
  cached.model = LoadModelFromMesh(mesh);
  if (cached.model.materialCount > 0) {
    SetMaterialTexture(&cached.model.materials[0], MATERIAL_MAP_DIFFUSE, chunk.blockSet->atlas);
  }
  cached.hasModel = true;
  return cached;
}

void VoxelRenderer::drawWorld(const World& world) {
  std::vector<RenderChunk> chunks;
  chunks.reserve(world.entities.size());
  for (const Entity& entity : world.entities) {
    if (!entity.voxelChunk.has_value() || !entity.transform3D.has_value()) {
      continue;
    }

    const auto blockSet = blockSets_.find(entity.voxelChunk->blockSet);
    if (blockSet == blockSets_.end()) {
      continue;
    }
    const Vec3 origin = worldPosition3D(world, entity);
    chunks.push_back(RenderChunk{
      .entity = &entity,
      .blockSet = &blockSet->second,
      .data = buildVoxelChunkFromComponent(*entity.voxelChunk, blockSet->second.blockSet, origin),
      .origin = chunkOrigin(origin),
      .signature = chunkSignature(entity, origin),
    });
  }

  std::ostringstream worldSignature;
  for (const RenderChunk& chunk : chunks) {
    worldSignature << chunk.entity->id << '=' << chunk.signature << ';';
  }
  const std::string neighborSignature = worldSignature.str();
  for (RenderChunk& chunk : chunks) {
    chunk.signature += "|neighbors{" + neighborSignature + '}';
  }

  for (const RenderChunk& chunk : chunks) {
    auto cached = chunkMeshes_.find(chunk.entity->id);
    if (cached == chunkMeshes_.end() || cached->second.signature != chunk.signature) {
      if (cached != chunkMeshes_.end() && cached->second.hasModel) {
        UnloadModel(cached->second.model);
      }
      cached = chunkMeshes_.insert_or_assign(chunk.entity->id, buildChunkMesh(chunk, chunks)).first;
      std::cerr << "Voxel debug: built " << chunk.entity->id << " with " << cached->second.faceCount << " visible face(s).\n";
    }

    if (!cached->second.hasModel) {
      continue;
    }

    const Vec3 position = worldPosition3D(world, *chunk.entity);
    const Vec3 rotation = worldRotation3D(world, *chunk.entity);
    rlPushMatrix();
    rlTranslatef(position.x, position.y, position.z);
    rlRotatef(rotation.x * (180.0F / 3.14159265358979F), 1.0F, 0.0F, 0.0F);
    rlRotatef(rotation.y * (180.0F / 3.14159265358979F), 0.0F, 1.0F, 0.0F);
    rlRotatef(rotation.z * (180.0F / 3.14159265358979F), 0.0F, 0.0F, 1.0F);
    DrawModel(cached->second.model, {0.0F, 0.0F, 0.0F}, 1.0F, ::Color{255, 255, 255, 255});
    rlPopMatrix();

    if (chunk.entity->voxelChunk->debug) {
      const Vector3 size{static_cast<float>(chunk.entity->voxelChunk->width), static_cast<float>(chunk.entity->voxelChunk->height), static_cast<float>(chunk.entity->voxelChunk->depth)};
      const Vector3 center{position.x + size.x * 0.5F, position.y + size.y * 0.5F, position.z + size.z * 0.5F};
      DrawCubeWiresV(center, size, {245, 245, 245, 255});
    }
  }
}

} // namespace demi::runtime
