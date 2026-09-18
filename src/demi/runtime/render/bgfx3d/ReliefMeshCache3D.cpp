#include "demi/runtime/render/bgfx3d/ReliefMeshCache3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
namespace demi::runtime::render {
void ReliefMeshCache3D::clear() {
  meshes_.clear();
  images_.clear();
  sources_.clear();
}
void ReliefMeshCache3D::loadAssets(const AssetRegistry &registry) {
  clear();
  for (const auto &asset : registry.assets)
    if (asset.type == "Texture2D")
      sources_[asset.id] = asset.sourcePath;
}
const GpuMesh3D *ReliefMeshCache3D::get(const SurfaceRelief3DComponent &r,
                                        Vec3 size, std::string &error) {
  const float thickness = std::abs(size.z);
  for (float n : {r.segments.x, r.segments.y})
    if (!std::isfinite(n) || n < 1 || n > 64 || std::floor(n) != n) {
      error = "Invalid relief subdivision count";
      return nullptr;
    }
  for (float n : {r.depth, r.heightMin, r.heightMax, r.uvOffset.x, r.uvOffset.y,
                  r.uvScale.x, r.uvScale.y})
    if (!std::isfinite(n)) {
      error = "Non-finite relief parameters";
      return nullptr;
    }
  if (r.heightMin >= r.heightMax || r.depth < 0) {
    error = "Invalid relief height range";
    return nullptr;
  }
  if (!std::isfinite(thickness) || thickness < .0001F ||
      r.depth > thickness * .45F) {
    error =
        "Surface relief must remain inside its box (depth <= 45% of thickness)";
    return nullptr;
  }
  const auto key =
      nlohmann::json({r.heightMap, r.depth / thickness, r.heightMin,
                      r.heightMax, r.uvOffset.x, r.uvOffset.y, r.uvScale.x,
                      r.uvScale.y, r.segments.x, r.segments.y})
          .dump();
  if (auto found = meshes_.find(key); found != meshes_.end()) {
    found->second.frame = frame_;
    return found->second.mesh.get();
  }
  if (meshes_.size() >= 256) {
    const auto unused = std::ranges::find_if(meshes_, [&](const auto &entry) {
      return entry.second.frame != frame_;
    });
    if (unused == meshes_.end()) {
      error = "Current view exceeds 256 shared relief variants";
      return nullptr;
    }
    meshes_.erase(unused); // Never invalidate a mesh queued by this view.
  }
  ProfileScope generation("Renderer3D.relief_generate");
  auto image = images_.find(r.heightMap);
  if (image == images_.end()) {
    auto path = sources_.find(r.heightMap);
    if (path == sources_.end()) {
      error = "Relief height map is not resident: " + r.heightMap;
      return nullptr;
    }
    std::ifstream file(path->second, std::ios::binary | std::ios::ate);
    const auto count = file.tellg();
    if (!file || count <= 0 || count > 16 * 1024 * 1024) {
      error = "Relief image must be readable and <=16 MiB";
      return nullptr;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(count));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), count)) {
      error = "Relief image read failed";
      return nullptr;
    }
    ImageData2D decoded;
    if (!decodeImage2D(bytes, decoded, error))
      return nullptr;
    if (decoded.width > 4096 || decoded.height > 4096) {
      error = "Relief image dimensions exceed 4096";
      return nullptr;
    }
    auto bytesUsed = [&] {
      std::size_t total = decoded.rgba.size();
      for (const auto &[id, value] : images_)
        total += value.rgba.size();
      return total;
    };
    while (!images_.empty() &&
           (images_.size() >= 8 || bytesUsed() > 64U * 1024U * 1024U))
      images_.erase(images_.begin());
    image = images_.emplace(r.heightMap, std::move(decoded)).first;
  }
  const auto &pixels = image->second;
  auto height = [&](Vec2 uv) {
    const float x = std::clamp(uv.x, 0.F, 1.F) * (pixels.width - 1),
                y = std::clamp(uv.y, 0.F, 1.F) * (pixels.height - 1);
    const int ix = int(x), iy = int(y);
    const float fx = x - ix, fy = y - iy;
    auto at = [&](int dx, int dy) {
      return float(std::to_integer<unsigned char>(
                 pixels.rgba[4 * (std::min(iy + dy, int(pixels.height) - 1) *
                                      pixels.width +
                                  std::min(ix + dx, int(pixels.width) - 1))])) /
             255;
    };
    const float h = (at(0, 0) * (1 - fx) + at(1, 0) * fx) * (1 - fy) +
                    (at(0, 1) * (1 - fx) + at(1, 1) * fx) * fy;
    return std::clamp((h - r.heightMin) / (r.heightMax - r.heightMin), 0.F,
                      1.F);
  };
  std::vector<Vec3> positions;
  std::vector<Vec2> uvs;
  std::vector<std::uint32_t> indices;
  const int nx = int(r.segments.x), ny = int(r.segments.y), stride = nx + 1,
            n = stride * (ny + 1);
  for (int side : {1, -1})
    for (int y = 0; y <= ny; ++y)
      for (int x = 0; x <= nx; ++x) {
        const Vec2 uv{r.uvOffset.x + float(x) / nx * r.uvScale.x,
                      r.uvOffset.y + float(y) / ny * r.uvScale.y};
        const float depth = .5F - (1 - height(uv)) * r.depth / thickness;
        positions.push_back(
            {float(x) / nx - .5F, .5F - float(y) / ny, side * depth});
        uvs.push_back(uv);
      }
  for (int offset : {0, n})
    for (int y = 0; y < ny; ++y)
      for (int x = 0; x < nx; ++x) {
        const auto a = std::uint32_t(offset + y * stride + x),
                   s = std::uint32_t(stride);
        if (offset == 0)
          indices.insert(indices.end(),
                         {a, a + s, a + s + 1, a, a + s + 1, a + 1});
        else
          indices.insert(indices.end(),
                         {a, a + 1, a + s + 1, a, a + s + 1, a + s});
      }
  std::vector<int> edge;
  for (int x = 0; x <= nx; ++x)
    edge.push_back(x);
  for (int y = 1; y <= ny; ++y)
    edge.push_back(y * stride + nx);
  for (int x = nx - 1; x >= 0; --x)
    edge.push_back(ny * stride + x);
  for (int y = ny - 1; y > 0; --y)
    edge.push_back(y * stride);
  for (std::size_t i = 0; i < edge.size(); ++i) {
    const int a = edge[i], b = edge[(i + 1) % edge.size()];
    const auto base = std::uint32_t(positions.size());
    for (int index : {a, b, b + n, a + n}) {
      const auto p = positions[index];
      const auto uv = uvs[index];
      positions.push_back(p);
      uvs.push_back(uv);
    }
    indices.insert(indices.end(),
                   {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  auto mesh = std::make_unique<GpuMesh3D>(resources_);
  if (!mesh->upload(positions, uvs, indices, 0xffffffffU, error))
    return nullptr;
  const auto *result = mesh.get();
  meshes_.emplace(key, Entry{std::move(mesh), frame_});
  RuntimeProfiler::setGauge("Renderer3D.relief_cache_variants",double(meshes_.size()));
  return result;
}
} // namespace demi::runtime::render
