#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetImporter.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/GltfSkinnedModel.h"
#include "demi/assets/ModelLodGenerator.h"
#include "demi/schema/Validation.h"
#include "fixtures/RedPixelPng.h"
#include <algorithm>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>

using namespace demi;
using namespace demi::assets;
using Json = nlohmann::json;
namespace {
void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
void write(const std::filesystem::path &path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(text.data(), text.size());
  require(bool(output), "Fixture write failed: " + path.string());
}
Json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return Json::parse(input);
}
void append(std::vector<std::byte> &bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 255));
  }
}

std::filesystem::path makeModel(const std::filesystem::path &project) {
  const auto directory = project / "source";
  Json source{
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", Json::array({{{"nodes", {0}}}})},
      {"nodes", Json::array({{{"mesh", 0}, {"translation", {4, 2, -7}}}})},
      {"meshes", Json::array({{{"primitives", Json::array()}}})},
      {"buffers", Json::array()},
      {"bufferViews", Json::array()},
      {"accessors", Json::array()},
      {"images", Json::array({{{"uri", "albedo%20image.png"}}})},
      {"samplers", Json::array({{{"wrapS", 10497},
                                 {"wrapT", 33648},
                                 {"minFilter", 9986},
                                 {"magFilter", 9728}}})},
      {"textures", Json::array({{{"source", 0}, {"sampler", 0}}})},
      {"materials", Json::array({{{"name", "stone"},
                                  {"pbrMetallicRoughness",
                                   {{"baseColorFactor", {.4, .5, .6, 1}},
                                    {"baseColorTexture", {{"index", 0}}}}}},
                                 {{"name", "leaves"},
                                  {"doubleSided", true},
                                  {"alphaMode", "MASK"},
                                  {"alphaCutoff", .4},
                                  {"pbrMetallicRoughness",
                                   {{"baseColorFactor", {.8, 1, .1, 1}},
                                    {"baseColorTexture", {{"index", 0}}}}}}})}};
  std::vector<std::byte> binary;
  const auto attribute = [&](const std::vector<float> &values,
                             unsigned components, const std::string &type) {
    const auto offset = binary.size();
    for (const float value : values) {
      append(binary, std::bit_cast<std::uint32_t>(value));
    }
    const auto view = source["bufferViews"].size();
    source["bufferViews"].push_back({{"buffer", 0},
                                     {"byteOffset", offset},
                                     {"byteLength", binary.size() - offset}});
    const auto accessor = source["accessors"].size();
    source["accessors"].push_back({{"bufferView", view},
                                   {"componentType", 5126},
                                   {"count", values.size() / components},
                                   {"type", type}});
    return accessor;
  };
  constexpr unsigned Side = 18;
  for (unsigned material = 0; material < 2; ++material) {
    std::vector<float> positions, normals, uvs, colors, tangents;
    for (unsigned y = 0; y <= Side; ++y) {
      for (unsigned x = 0; x <= Side; ++x) {
        positions.insert(positions.end(),
                         {float(x) + material * 30.F, 0, float(y)});
        normals.insert(normals.end(), {0, 1, 0});
        uvs.insert(uvs.end(),
                   {float(x) / Side * 3 - 1, float(y) / Side * 3 - 1});
        colors.insert(colors.end(), {.8F, .7F, .6F, 1});
        tangents.insert(tangents.end(), {1, 0, 0, 1});
      }
    }
    const auto position = attribute(positions, 3, "VEC3");
    source["accessors"][position]["min"] = {material * 30, 0, 0};
    source["accessors"][position]["max"] = {material * 30 + Side, 0, Side};
    const Json attributes{{"POSITION", position},
                          {"NORMAL", attribute(normals, 3, "VEC3")},
                          {"TEXCOORD_0", attribute(uvs, 2, "VEC2")},
                          {"COLOR_0", attribute(colors, 4, "VEC4")},
                          {"TANGENT", attribute(tangents, 4, "VEC4")}};
    const auto offset = binary.size();
    for (unsigned y = 0; y < Side; ++y) {
      for (unsigned x = 0; x < Side; ++x) {
        const auto a = y * (Side + 1) + x;
        for (const auto index :
             {a, a + Side + 1, a + 1, a + 1, a + Side + 1, a + Side + 2}) {
          append(binary, index);
        }
      }
    }
    const auto view = source["bufferViews"].size();
    source["bufferViews"].push_back({{"buffer", 0},
                                     {"byteOffset", offset},
                                     {"byteLength", binary.size() - offset}});
    const auto accessor = source["accessors"].size();
    source["accessors"].push_back({{"bufferView", view},
                                   {"componentType", 5125},
                                   {"count", Side * Side * 6},
                                   {"type", "SCALAR"}});
    source["meshes"][0]["primitives"].push_back({{"attributes", attributes},
                                                 {"indices", accessor},
                                                 {"material", material},
                                                 {"mode", 4}});
  }
  source["buffers"].push_back(
      {{"uri", "geometry.bin"}, {"byteLength", binary.size()}});
  write(directory / "model.gltf", source.dump());
  write(directory / "geometry.bin",
        {reinterpret_cast<const char *>(binary.data()), binary.size()});
  const auto png = demi::tests::redPixelPng();
  write(directory / "albedo image.png",
        {reinterpret_cast<const char *>(png.data()), png.size()});
  const auto imported = importAsset({.projectDirectory = project,
                                     .source = directory / "model.gltf",
                                     .id = "asset://models/grid"});
  require(!hasErrors(imported.diagnostics), "Grid model import failed");
  return imported.manifestPath;
}

using Vertex = std::array<float, 12>;
Vertex vertex(const GltfSkinnedVertex3D &value) {
  return {value.position.x, value.position.y, value.position.z, value.uv.x,
          value.uv.y,       value.normal.x,   value.normal.y,   value.normal.z,
          value.color.r,    value.color.g,    value.color.b,    value.color.a};
}

void verifyGeneration(const std::filesystem::path &project,
                      const std::filesystem::path &manifestPath) {
  auto authored = readJson(manifestPath);
  authored["settings"]["author_values"] = {1, 2, 3};
  const std::string customSource =
      "{\n\t\"source\" : " + authored["source"].dump() +
      ",\n\t\"id\" : " + authored["id"].dump() +
      ",\n\t\"custom_author_note\" : { \"left\" : [1,  2], \"right\" : true "
      "}," +
      "\n\t\"format_version\" : 1,\n\t\"type\" : \"Model3D\"," +
      "\n\t\"source_hash\" : " + authored["source_hash"].dump() +
      ",\n\t\"generated_output\" : " + authored["generated_output"].dump() +
      ",\n\t\"importer\" : \"gltf-model\",\n\t\"importer_version\" : 1," +
      "\n\t\"dependencies\" : [],\n\t\"settings\" : { \"model_import\" : " +
      authored["settings"]["model_import"].dump() +
      ", \"author_values\" : [1.000, 2,3] }\n}\n";
  write(manifestPath, customSource);
  const auto manifest = loadAssetManifest(manifestPath);
  require(manifest.has_value(), "Source manifest missing");
  const auto sourceHash = hashFile(manifest->sourcePath);
  std::string error;
  const auto original = loadGltfSkinnedModel3D(
      manifest->sourcePath, modelImportPreset("static_prop"), error);
  require(original.has_value(), error);
  std::set<Vertex> originalVertices;
  for (const auto &value : original->vertices) {
    originalVertices.insert(vertex(value));
  }
  const auto generated = generateModelLods(
      {.projectDirectory = project, .modelManifestPath = manifestPath});
  if (hasErrors(generated.diagnostics)) {
    printDiagnosticsText(std::cerr, generated.diagnostics);
  }
  require(!hasErrors(generated.diagnostics) && generated.variants.size() == 2,
          "LOD generation failed");
  require(hashFile(manifest->sourcePath) == sourceHash,
          "LOD generation changed original source bytes");
  std::ifstream formattedInput(manifestPath);
  const std::string formatted{std::istreambuf_iterator<char>(formattedInput),
                              std::istreambuf_iterator<char>()};
  require(formatted.starts_with("{\n\t\"source\" : ") &&
              formatted.find("\t\"custom_author_note\" : { \"left\" : [1,  2], "
                             "\"right\" : true },") != std::string::npos &&
              formatted.find("\"author_values\" : [1.000, 2,3]") !=
                  std::string::npos &&
              formatted.find("\"id\"") < formatted.find("\"format_version\""),
          "LOD recipe edits reformatted unrelated manifest source");
  require(readJson(manifestPath)["settings"]["model_lods"]["format_version"] ==
              1,
          "LOD recipe did not persist");
  std::vector<std::string> outputHashes;
  for (const auto &variant : generated.variants) {
    const auto imported = loadAssetManifest(variant.manifestPath);
    require(imported.has_value(), "Generated manifest missing");
    const auto model = loadGltfSkinnedModel3D(
        imported->sourcePath, modelImportPreset("static_prop"), error);
    require(model.has_value(), error);
    require(variant.sourceTriangles == original->indices.size() / 3 &&
                variant.triangles == model->indices.size() / 3 &&
                variant.triangles < variant.sourceTriangles,
            "LOD triangle counts did not reflect real simplification");
    require(model->vertices.size() < original->vertices.size(),
            "LOD vertex streams were not compacted");
    require(model->materialSections.size() == original->materialSections.size(),
            "LOD collapsed material sections");
    require(model->nodes.size() == original->nodes.size() &&
                model->nodes[0].localMatrix == original->nodes[0].localMatrix,
            "LOD changed node transforms");
    for (const auto &value : model->vertices) {
      require(originalVertices.contains(vertex(value)),
              "LOD changed UV/normal/color vertex values");
    }
    for (const auto index : model->indices) {
      require(index < model->vertices.size(),
              "LOD emitted out-of-range indices");
    }
    for (std::size_t index = 0; index < model->materialSections.size();
         ++index) {
      require(model->materialSections[index].material ==
                  original->materialSections[index].material,
              "LOD reassigned primitive materials");
    }
    const auto &texture = model->materials[0].textures.at(GltfMapRole::Albedo);
    const auto &sourceTexture =
        original->materials[0].textures.at(GltfMapRole::Albedo);
    require(texture.wrapU == sourceTexture.wrapU &&
                texture.wrapV == sourceTexture.wrapV &&
                texture.minFilter == sourceTexture.minFilter &&
                texture.magFilter == sourceTexture.magFilter &&
                *texture.encoded == *sourceTexture.encoded,
            "LOD changed source images or sampler semantics");
    require(model->materials[1].alphaMode == GltfAlphaMode::Mask &&
                model->materials[1].doubleSided,
            "LOD lost mask/double-sided materials");
    outputHashes.push_back(*hashFile(imported->sourcePath));
  }
  require(generated.variants[1].triangles <= generated.variants[0].triangles,
          "Low LOD is denser than medium");
  const auto repeated = generateModelLods(
      {.projectDirectory = project, .modelManifestPath = manifestPath});
  require(!hasErrors(repeated.diagnostics), "Repeated LOD generation failed");
  for (std::size_t index = 0; index < repeated.variants.size(); ++index) {
    require(hashFile(loadAssetManifest(repeated.variants[index].manifestPath)
                         ->sourcePath) == outputHashes[index],
            "LOD output is not deterministic");
  }
  require(!hasErrors(reimportAsset(manifestPath)), "Recipe reimport failed");
  const auto registry = loadAssetRegistry(project);
  const auto validation = validateAssetRegistry(registry);
  if (hasErrors(validation)) {
    printDiagnosticsText(std::cerr, validation);
  }
  require(!hasErrors(validation), "Generated model registry does not validate");
  const auto cooked = project.parent_path() / "cooked";
  const auto diagnostics =
      cookProject({.projectFile = project / "demi.project.json",
                   .outputDirectory = cooked});
  if (hasErrors(diagnostics)) {
    printDiagnosticsText(std::cerr, diagnostics);
  }
  require(!hasErrors(diagnostics), "LOD project cook failed");
  const auto cookedRegistry = loadAssetRegistry(cooked);
  const auto *cookedBase = findAsset(cookedRegistry, manifest->id);
  require(cookedBase &&
              Json::parse(cookedBase->settingsJson).contains("model_lods"),
          "Cook dropped LOD recipe");
  require(findAsset(cookedRegistry, generated.variants[0].id) &&
              findAsset(cookedRegistry, generated.variants[1].id),
          "Cook dropped generated LOD identities");
}

void verifyFreshCheckoutAndNoOpWrites(
    const std::filesystem::path &project,
    const std::filesystem::path &manifestPath) {
  const auto registry = loadAssetRegistry(project);
  const auto *medium = findAsset(registry, "asset://models/grid/lod/medium");
  const auto *low = findAsset(registry, "asset://models/grid/lod/low");
  require(medium && low, "Generated LOD fixtures missing");
  const auto mediumPath = medium->sourcePath;
  const auto mediumManifest = medium->manifestPath;
  const auto before = hashFile(mediumPath);
  const auto oldTime =
      std::filesystem::file_time_type{} + std::chrono::hours(1);
  for (const auto &path : {manifestPath, mediumPath, mediumManifest,
                           low->sourcePath, low->manifestPath}) {
    std::filesystem::last_write_time(path, oldTime);
  }
  require(!hasErrors(ensureModelLods(project)), "Current LOD preflight failed");
  const auto repeated = generateModelLods(
      {.projectDirectory = project, .modelManifestPath = manifestPath});
  require(!hasErrors(repeated.diagnostics), "No-op explicit generation failed");
  for (const auto &path : {manifestPath, mediumPath, mediumManifest,
                           low->sourcePath, low->manifestPath}) {
    require(std::filesystem::last_write_time(path) == oldTime,
            "Unchanged generation rewrote existing artifacts");
  }
  std::filesystem::remove_all(mediumPath.parent_path());
  const auto scene = project / "scenes/needs-lods.scene.json";
  write(scene,
        Json{{"format_version", 1},
             {"id", "scene://lod/main"},
             {"entities",
              Json::array(
                  {{{"id", "mesh"},
                    {"components",
                     {{"Transform3D", Json::object()},
                      {"MeshRenderer",
                       {{"model", "asset://models/grid"},
                        {"medium_lod_model", "asset://models/grid/lod/medium"},
                        {"medium_lod_distance", 10},
                        {"low_lod_model", "asset://models/grid/lod/low"},
                        {"low_lod_distance", 20}}}}}}})}}
            .dump());
  const auto validation = validatePath(scene);
  require(std::ranges::any_of(validation.diagnostics,
                              [](const Diagnostic &value) {
                                return value.code ==
                                       "MODEL_LOD_GENERATION_REQUIRED";
                              }),
          "Fresh checkout did not explain the declared LOD generation recipe");
  require(std::ranges::none_of(validation.diagnostics,
                               [](const Diagnostic &value) {
                                 return value.code ==
                                        "ASSET_REFERENCE_NOT_FOUND";
                               }),
          "Declared generated LOD IDs were misreported as unknown assets");
  require(!std::filesystem::exists(mediumPath),
          "Read-only validation generated source assets");
  const auto cooked =
      cookProject({.projectFile = project / "demi.project.json",
                   .outputDirectory = project.parent_path() / "cooked-fresh"});
  if (hasErrors(cooked)) {
    printDiagnosticsText(std::cerr, cooked);
  }
  require(!hasErrors(cooked) && hashFile(mediumPath) == before,
          "Cook did not deterministically restore missing LOD outputs from the "
          "source recipe");
  require(!hasErrors(validatePath(scene).diagnostics),
          "Regenerated LOD scene did not validate");
  const auto original = loadAssetManifest(manifestPath);
  const auto buffer = original->sourcePath.parent_path() / "geometry.bin";
  std::fstream geometry(buffer,
                        std::ios::binary | std::ios::in | std::ios::out);
  const auto changed = std::bit_cast<std::uint32_t>(.125F);
  for (unsigned shift = 0; shift < 32; shift += 8) {
    geometry.put(static_cast<char>((changed >> shift) & 255));
  }
  geometry.close();
  require(!hasErrors(ensureModelLods(project)) &&
              hashFile(mediumPath) != before,
          "Source geometry edits did not regenerate stale LOD payloads");
  require(!hasErrors(validateAssetRegistry(loadAssetRegistry(project))),
          "Source regeneration did not refresh registry metadata");
}

void verifyFailuresLeavePreviousOutput(
    const std::filesystem::path &project,
    const std::filesystem::path &manifestPath) {
  const auto beforeManifest = hashFile(manifestPath);
  const auto registry = loadAssetRegistry(project);
  const auto output =
      findAsset(registry, "asset://models/grid/lod/medium")->sourcePath;
  const auto beforeOutput = hashFile(output);
  const auto invalid = generateModelLods({.projectDirectory = project,
                                          .modelManifestPath = manifestPath,
                                          .mediumRatio = .1F,
                                          .lowRatio = .5F});
  require(hasErrors(invalid.diagnostics) &&
              hashFile(manifestPath) == beforeManifest &&
              hashFile(output) == beforeOutput,
          "Invalid parameters changed existing LODs");
  const auto manifest = loadAssetManifest(manifestPath);
  auto source = readJson(manifest->sourcePath);
  const auto original = source;
  source["skins"] = Json::array({{{"joints", {0}}}});
  write(manifest->sourcePath, source.dump());
  const auto skinned = generateModelLods(
      {.projectDirectory = project, .modelManifestPath = manifestPath});
  require(hasErrors(skinned.diagnostics) &&
              skinned.diagnostics.back().message.find("skinned") !=
                  std::string::npos &&
              hashFile(output) == beforeOutput,
          "Skinned model rejection changed previous outputs");
  source = original;
  source["animations"] =
      Json::array({{{"channels", Json::array()}, {"samplers", Json::array()}}});
  write(manifest->sourcePath, source.dump());
  const auto animated = generateModelLods(
      {.projectDirectory = project, .modelManifestPath = manifestPath});
  require(hasErrors(animated.diagnostics) &&
              animated.diagnostics.back().message.find("animated") !=
                  std::string::npos &&
              hashFile(output) == beforeOutput &&
              hashFile(manifestPath) == beforeManifest,
          "Animated model rejection changed previous outputs");
  source = original;
  for (auto &primitive : source["meshes"][0]["primitives"]) {
    primitive["targets"] =
        Json::array({{{"POSITION", primitive["attributes"]["POSITION"]}}});
  }
  write(manifest->sourcePath, source.dump());
  const auto morph = generateModelLods(
      {.projectDirectory = project, .modelManifestPath = manifestPath});
  require(hasErrors(morph.diagnostics) &&
              morph.diagnostics.back().message.find("morph") !=
                  std::string::npos &&
              hashFile(output) == beforeOutput,
          "Morph rejection changed previous outputs");
  write(manifest->sourcePath, original.dump());
  const auto originalManifest = readJson(manifestPath);
  auto invalidMirror = originalManifest;
  invalidMirror["generated_output"] =
      std::filesystem::relative(project / "generated",
                                manifestPath.parent_path())
          .generic_string();
  write(manifestPath, invalidMirror.dump());
  const auto invalidMirrorHash = hashFile(manifestPath);
  const auto mirrorFailure = generateModelLods(
      {.projectDirectory = project, .modelManifestPath = manifestPath});
  require(hasErrors(mirrorFailure.diagnostics) &&
              std::filesystem::is_directory(project / "generated") &&
              hashFile(output) == beforeOutput &&
              hashFile(manifestPath) == invalidMirrorHash,
          "Directory-valued generated mirror damaged prior artifacts");
  write(manifestPath, originalManifest.dump(2) + "\n");

  const auto external = project.parent_path() / "outside";
  std::filesystem::create_directory(external);
  const auto generatedDirectory = project / "assets/generated";
  const auto backup = project / "assets/generated-owned";
  std::filesystem::rename(generatedDirectory, backup);
  std::error_code symlinkError;
  std::filesystem::create_directory_symlink(external, generatedDirectory,
                                            symlinkError);
  if (!symlinkError) {
    const auto escaped = generateModelLods(
        {.projectDirectory = project, .modelManifestPath = manifestPath});
    require(hasErrors(escaped.diagnostics) &&
                std::filesystem::is_empty(external),
            "LOD output followed a symlink outside the project");
    std::filesystem::remove(generatedDirectory);
  }
  std::filesystem::rename(backup, generatedDirectory);
  require(hashFile(output) == beforeOutput,
          "Rejected output path changed previous artifacts");
  require(
      hasErrors(validateModelLodSettings(
          {{"model_lods", {{"format_version", 1}, {"medium_ratio", "bad"}}}},
          manifestPath)),
      "Invalid recipe passed validation");
}
} // namespace
int main() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("demi-model-lods-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto project = root / "project";
  try {
    write(project / "demi.project.json",
          R"({"format_version":1,"name":"LOD fixture","scenes":[]})");
    const auto manifest = makeModel(project);
    verifyGeneration(project, manifest);
    verifyFreshCheckoutAndNoOpWrites(project, manifest);
    verifyFailuresLeavePreviousOutput(project, manifest);
    std::filesystem::remove_all(root);
    std::cout << "Static model LOD generation, material/UV preservation, "
                 "recipe, cook and rollback passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << " fixture=" << root << '\n';
    return 1;
  }
}
