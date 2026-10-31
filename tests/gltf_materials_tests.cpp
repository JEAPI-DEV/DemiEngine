#include "demi/assets/GltfSkinnedModel.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx3d/GpuSkinnedMesh3D.h"
#include "demi/runtime/render/bgfx3d/ImportedModelMaterials3D.h"
#include "demi/runtime/render/bgfx3d/MeshInstanceBatch3D.h"
#include "demi/runtime/render/bgfx3d/MeshVertexPreparation3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/scene/model/World.h"
#include "fixtures/RedPixelPng.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace demi;
using namespace demi::assets;
using namespace demi::runtime::render;
using Json = nlohmann::json;

namespace {

void write(const std::filesystem::path &path, const Json &document) {
  std::ofstream(path) << document.dump();
}

class RecordingCommands final : public RenderCommands {
public:
  bool configureView2D(const View2DConfig &, std::string &) override {
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return true;
  }
  bool submit(const TransientDraw &, std::string &) override { return true; }
  bool submit(const InstancedBufferedDraw &draw, std::string &) override {
    ranges.emplace_back(draw.indices.first, draw.indices.count);
    return true;
  }
  bool submit(const BufferedDraw &draw, std::string &) override {
    ranges.emplace_back(draw.indices.first, draw.indices.count);
    return true;
  }
  std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
};

void staticAndSkinnedDrawsUseSectionRanges(const GltfSkinnedModel3D &model) {
  std::string error;
  BgfxGraphicsDevice device;
  assert(device.initialize(
      {.api = GraphicsApi::Noop, .width = 32, .height = 32}, error));
  auto resources = createBgfxGpuResources();
  RecordingCommands commands;
  const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0,
                                       0, 0, 1, 0, 0, 0, 0, 1};
  std::vector<demi::runtime::Vec3> positions;
  std::vector<demi::runtime::Vec3> normals;
  assert(model.bindPosePositions(positions, error));
  assert(model.bindPoseNormals(normals, error));
  assert(normals.size() == model.vertices.size() && normals.front().z == 1);
  std::vector<demi::runtime::Vec2> uvs;
  for (const auto &vertex : model.vertices) {
    uvs.push_back(vertex.uv);
  }
  GpuMesh3D mesh(*resources);
  assert(
      mesh.upload(positions, uvs, model.indices, 0xffffffffU, error, normals));
  assert(mesh.draw(commands, 0, {}, {}, {}, identity, {}, error, {}, 3, 3));
  assert(commands.ranges.back() == std::make_pair(3U, 3U));
  const std::array transforms{identity, identity};
  assert(mesh.drawInstanced(commands, 0, {}, {}, {}, transforms, {}, error, {},
                            3, 3));
  assert(commands.ranges.back() == std::make_pair(3U, 3U));
  demi::runtime::MeshRendererComponent source;
  PreparedMeshDraw3D prepared(source, nullptr, {}, false, false, false, {});
  MeshInstanceBatch3D batch;
  const auto first =
      batch.addSurface(mesh, {}, source, nullptr, prepared, false, identity, 0, 3);
  const auto second =
      batch.addSurface(mesh, {}, source, nullptr, prepared, false, identity, 3, 3);
  const auto repeated =
      batch.addSurface(mesh, {}, source, nullptr, prepared, false, identity, 0, 3);
  assert(first != second && first == repeated);
  // A rigid animated node is a real GPU palette input; an ordinary static
  // model correctly bypasses skinning in production.
  auto animated = model;
  animated.clips.push_back({.name = "move", .duration = 1,
      .channels = {{.node = 0,
                    .path = GltfSkinnedModel3D::ChannelPath::Translation,
                    .times = {0, 1},
                    .values = {{0, 0, 0, 0}, {1, 0, 0, 0}}}}});
  std::vector<demi::runtime::Vec3> moved;
  assert(animated.samplePositions(0, 0.5F, false, moved, error));
  assert(moved.front().x == 0.5F);
  std::vector<GpuSkinnedVertex3D> vertices;
  GpuSkinPaletteLayout layout;
  assert(buildGpuSkinVertices(animated, vertices, layout, error));
  GpuSkinnedMesh3D skin(*resources);
  assert(skin.upload(vertices, animated.indices, std::move(layout), error));
  assert(skin.draw(commands, 0, {}, {}, {}, identity, {}, {}, error, 3, 3));
  assert(commands.ranges.back() == std::make_pair(3U, 3U));
  const auto submitted = commands.ranges.size();
  assert(!skin.draw(commands, 0, {}, {}, {}, identity, {}, {}, error, 4, 3));
  assert(commands.ranges.size() == submitted);
}

void materialSectionsPreserveSourceAssignments(
    const std::filesystem::path &root) {
  const std::array<float, 24> data{0,  0, 0, 1, 0, 0, 0, 1, 0, -2, -3, 2,
                                   -3, 0, 4, 0, 0, 1, 0, 0, 1, 0,  0,  1};
  std::ofstream buffer(root / "mesh.bin", std::ios::binary);
  buffer.write(reinterpret_cast<const char *>(data.data()), sizeof(data));
  buffer.close();
  const auto image = demi::tests::redPixelPng();
  std::ofstream pixels(root / "pixel.png", std::ios::binary);
  pixels.write(reinterpret_cast<const char *>(image.data()), image.size());
  pixels.close();
  Json document = {
      {"asset", {{"version", "2.0"}}},
      {"buffers",
       Json::array({{{"uri", "mesh.bin"}, {"byteLength", sizeof(data)}}})},
      {"bufferViews",
       Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                    {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 24}},
                    {{"buffer", 0}, {"byteOffset", 60}, {"byteLength", 36}}})},
      {"accessors", Json::array({{{"bufferView", 0},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"}},
                                 {{"bufferView", 1},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC2"}},
                                 {{"bufferView", 2},
                                  {"componentType", 5126},
                                  {"count", 3},
                                  {"type", "VEC3"}}})},
      {"images", Json::array({{{"uri", "pixel.png"}}})},
      {"samplers", Json::array({{{"wrapS", 33648},
                                 {"wrapT", 33071},
                                 {"minFilter", 9985},
                                 {"magFilter", 9728}}})},
      {"textures",
       Json::array({{{"source", 0}}, {{"source", 0}, {"sampler", 0}}})},
      {"materials", Json::array({{{"name", "first"},
                                  {"pbrMetallicRoughness",
                                   {{"baseColorFactor", {0.5, 1, 1, 1}},
                                    {"baseColorTexture", {{"index", 0}}},
                                    {"roughnessFactor", 0.3},
                                    {"metallicFactor", 0.7}}}},
                                 {{"name", "second"},
                                  {"pbrMetallicRoughness",
                                   {{"baseColorTexture", {{"index", 1}}}}},
                                  {"doubleSided", true}}})},
      {"meshes",
       Json::array(
           {{{"primitives",
              Json::array(
                  {{{"attributes",
                     {{"POSITION", 0}, {"TEXCOORD_0", 1}, {"NORMAL", 2}}},
                    {"material", 0}},
                   {{"attributes",
                     {{"POSITION", 0}, {"TEXCOORD_0", 1}, {"NORMAL", 2}}},
                    {"material", 1}}})}}})},
      {"nodes", Json::array({{{"mesh", 0}}})},
      {"scenes", Json::array({{{"nodes", {0}}}})},
      {"scene", 0}};
  const auto path = root / "materials.gltf";
  write(path, document);
  std::string error;
  const auto model = loadGltfSkinnedModel3D(path, error);
  assert(model && error.empty());
  assert(model->materialSections.size() == 2);
  assert(model->materialSections[0].material == 0);
  assert(model->materialSections[0].firstIndex == 0);
  assert(model->materialSections[0].indexCount == 3);
  assert(model->materialSections[1].material == 1);
  assert(model->materialSections[1].firstIndex == 3);
  assert(model->materialSections[1].indexCount == 3);
  assert(model->vertices[0].uv.x == -2 && model->vertices[0].uv.y == -3);
  assert(model->vertices[0].color.r == 1);
  assert(model->materials[0].baseColor.r == 0.5F);
  const auto &firstTexture =
      model->materials[0].textures.at(GltfMapRole::Albedo);
  assert(firstTexture.wrapU == GltfWrap::Repeat &&
         firstTexture.wrapV == GltfWrap::Repeat);
  const auto &secondTexture =
      model->materials[1].textures.at(GltfMapRole::Albedo);
  assert(secondTexture.wrapU == GltfWrap::Mirror &&
         secondTexture.wrapV == GltfWrap::Clamp);
  assert(secondTexture.encoded == firstTexture.encoded);

  AssetManifest asset{
      .id = "asset://fixture/model", .settingsJson = "{}", .sourcePath = path};
  PreparedImportedMaterial3D prepared;
  assert(prepareImportedMaterial(asset, 0, model->materials[0], false, prepared,
                                 error));
  assert(prepared.textures.size() == 1);
  assert(prepared.textures[0].texture.sampling.wrap == TextureWrap::Repeat);
  assert(prepared.binding.roughness == 0.3F &&
         prepared.binding.metallic == 0.7F);
  assert(prepared.binding.state.depthTest == DepthTest::Less);
  asset.settingsJson = R"({"wrap":"clamp","filter":"nearest","mipmaps":false})";
  asset.textureSettings.wrap = "clamp";
  asset.textureSettings.filter = "nearest";
  assert(prepareImportedMaterial(asset, 1, model->materials[1], false, prepared,
                                 error));
  assert(prepared.textures[0].texture.sampling.wrap == TextureWrap::Clamp);
  assert(prepared.textures[0].texture.sampling.wrapV == TextureWrap::Clamp);
  assert(prepared.binding.state.cull == CullMode::None);

  demi::runtime::MeshMaterialSections3D sections;
  sections.indices =
      std::make_shared<const std::vector<std::uint32_t>>(model->indices);
  for (const auto &section : model->materialSections) {
    sections.sections.push_back(
        {.id = std::to_string(section.material),
         .material = importedMaterialId(asset.id, section.material),
         .firstIndex = section.firstIndex,
         .indexCount = section.indexCount});
  }
  demi::runtime::World world;
  demi::runtime::Entity entity;
  entity.id = "model";
  entity.setComponent(demi::runtime::Transform3DComponent{});
  entity.setComponent(demi::runtime::MeshRendererComponent{.model = asset.id});
  world.entities.push_back(std::move(entity));
  BgfxCameraFrame3D frame;
  frame.position = {0, 0, -5};
  frame.forward = {0, 0, 1};
  const auto extract = [&] {
    return extractVisibleMeshes3D(
        world, frame, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, {}, nullptr, nullptr,
        [&](const VisibleMesh3D &, demi::runtime::Vec3) { return &sections; });
  };
  const auto visible = extract();
  assert(visible.meshes.size() == 2);
  assert(visible.meshes[0].materialSection == 0);
  assert(visible.meshes[1].materialSection == 1);
  world.entities[0]
      .component<demi::runtime::MeshRendererComponent>()
      ->material = "asset://override";
  const auto overridden = extract();
  assert(overridden.meshes.size() == 1 &&
         !overridden.meshes[0].materialSection);

  staticAndSkinnedDrawsUseSectionRanges(*model);

  auto cutoutMaterial = model->materials[0];
  cutoutMaterial.alphaMode = GltfAlphaMode::Mask;
  cutoutMaterial.alphaCutoff = .25F;
  cutoutMaterial.baseColor.a = .5F;
  cutoutMaterial.textures.at(GltfMapRole::Albedo).minFilter = GltfFilter::LinearMipmapLinear;
  asset.settingsJson = "{}";
  assert(prepareImportedMaterial(asset, 0, cutoutMaterial, false, prepared, error));
  assert(prepared.textures.front().texture.sampling.alphaCoverageCutoff == .5F);
  assert(prepared.textures.front().texture.sampling.mipmaps);
  cutoutMaterial.alphaMode = GltfAlphaMode::Blend;
  assert(prepareImportedMaterial(asset, 0, cutoutMaterial, false, prepared, error));
  assert(!prepared.textures.front().texture.sampling.alphaCoverageCutoff);

  auto packedMaterial = model->materials[0];
  packedMaterial.textures.emplace(GltfMapRole::MetallicRoughness, firstTexture);
  assert(prepareImportedMaterial(asset, 0, packedMaterial, false, prepared,
                                 error));
  for (const auto &texture : prepared.textures) {
    if (texture.slot == MaterialMapSlot::Metallic ||
        texture.slot == MaterialMapSlot::Roughness) {
      // The red source has zero G/B. Reading R would incorrectly yield a
      // nonzero scalar even after the material factor is applied.
      assert(texture.texture.rgbaMipBytes[0] == std::byte{0});
    }
  }

  for (const bool mixedNormals : {false, true}) {
    auto missingNormals = document;
    auto &primitives = missingNormals["meshes"][0]["primitives"];
    primitives[0]["attributes"].erase("NORMAL");
    if (!mixedNormals) {
      primitives[1]["attributes"].erase("NORMAL");
    }
    write(path, missingNormals);
    const auto incomplete = loadGltfSkinnedModel3D(path, error);
    assert(incomplete);
    std::vector<demi::runtime::Vec3> normals{{9, 9, 9}};
    assert(incomplete->bindPoseNormals(normals, error));
    assert(normals.empty());
    std::vector<demi::runtime::Vec3> positions;
    assert(incomplete->bindPosePositions(positions, error));
    std::vector<GpuMeshVertex3D> generated;
    assert(prepareMeshVertices3D(positions, {}, incomplete->indices,
                                 0xffffffffU, normals, {}, generated, error));
    assert(generated.size() == incomplete->vertices.size());
    for (const auto &vertex : generated) {
      assert(vertex.nx == 0 && vertex.ny == 0 && vertex.nz == 1);
    }
  }

  document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]
          ["texCoord"] = 1;
  write(path, document);
  assert(!loadGltfSkinnedModel3D(path, error));
  assert(error.find("UV set") != std::string::npos);
}

} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-gltf-materials-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  materialSectionsPreserveSourceAssignments(root);
  std::filesystem::remove_all(root);
}
