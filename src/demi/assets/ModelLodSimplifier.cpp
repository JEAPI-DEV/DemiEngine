#include "demi/assets/ModelLodSimplifier.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace demi::assets {
namespace {

std::string accessorType(cgltf_type type) {
  switch (type) {
  case cgltf_type_scalar:
    return "SCALAR";
  case cgltf_type_vec2:
    return "VEC2";
  case cgltf_type_vec3:
    return "VEC3";
  case cgltf_type_vec4:
    return "VEC4";
  default:
    return {};
  }
}

bool simplifyPrimitive(const cgltf_primitive &source, float ratio,
                       float maxError, gltf::ModelLodPrimitive &result,
                       float &achievedError, std::string &error) {
  const auto *position = gltf::attribute(source, cgltf_attribute_type_position);
  if (!position || position->type != cgltf_type_vec3 || position->count == 0 ||
      position->count > std::numeric_limits<std::uint32_t>::max()) {
    error = "Every static LOD primitive requires finite VEC3 positions and a "
            "representable vertex count.";
    return false;
  }
  const auto vertexCount = position->count;
  for (std::size_t index = 0; index < source.attributes_count; ++index) {
    const auto &attribute = source.attributes[index];
    if (!attribute.name || !attribute.data ||
        attribute.data->count != vertexCount) {
      error = "Model LOD attributes must name matching vertex streams.";
      return false;
    }
    gltf::ModelLodAttribute values;
    values.type = accessorType(attribute.data->type);
    values.components = cgltf_num_components(attribute.data->type);
    if (values.type.empty() ||
        vertexCount > values.values.max_size() / values.components) {
      error = "Model LOD generation supports scalar/vector vertex attributes.";
      return false;
    }
    values.values.resize(vertexCount * values.components);
    if (cgltf_accessor_unpack_floats(attribute.data, values.values.data(),
                                     values.values.size()) !=
            values.values.size() ||
        !std::ranges::all_of(
            values.values, [](float value) { return std::isfinite(value); })) {
      error = "Could not decode finite model LOD vertex attributes.";
      return false;
    }
    result.attributes.emplace(attribute.name, std::move(values));
  }
  const auto positionValues = result.attributes.find("POSITION");
  if (positionValues == result.attributes.end()) {
    error = "Model LOD primitive is missing POSITION.";
    return false;
  }
  std::vector<unsigned int> indices(source.indices ? source.indices->count
                                                   : vertexCount);
  if (indices.empty() || indices.size() % 3 != 0) {
    error =
        "Model LOD triangle index counts must be nonempty multiples of three.";
    return false;
  }
  if (source.indices) {
    if (cgltf_accessor_unpack_indices(source.indices, indices.data(),
                                      sizeof(unsigned int),
                                      indices.size()) != indices.size()) {
      error = "Could not decode model LOD triangle indices.";
      return false;
    }
  } else {
    std::iota(indices.begin(), indices.end(), 0U);
  }
  if (std::ranges::any_of(indices,
                          [&](auto index) { return index >= vertexCount; })) {
    error = "Model LOD triangle references a vertex outside its primitive.";
    return false;
  }
  // Material boundaries are separate simplification domains. Attribute-aware
  // error and locked open borders retain UV seams, normals and card edges.
  std::vector<std::pair<const gltf::ModelLodAttribute *, float>> metrics;
  std::vector<float> weights;
  for (const auto &[name, attribute] : result.attributes) {
    const float weight = name.starts_with("TEXCOORD_")             ? 1.F
                         : (name == "NORMAL" || name == "TANGENT") ? .5F
                         : name.starts_with("COLOR_")              ? .25F
                                                                   : 0.F;
    if (weight != 0) {
      if (weights.size() + attribute.components > 32) {
        error = "Model LOD generation supports at most 32 weighted "
                "UV/normal/color channels.";
        return false;
      }
      metrics.emplace_back(&attribute, weight);
      weights.insert(weights.end(), attribute.components, weight);
    }
  }
  std::vector<float> attributes(vertexCount * weights.size());
  if (!weights.empty()) {
    for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
      auto destination = attributes.begin() + vertex * weights.size();
      for (const auto &[attribute, weight] : metrics) {
        destination = std::copy_n(attribute->values.begin() +
                                      vertex * attribute->components,
                                  attribute->components, destination);
      }
    }
  }
  std::vector<unsigned int> simplified(indices.size());
  const auto targetTriangles = std::max<std::size_t>(
      1, std::size_t(std::floor(double(indices.size() / 3) * ratio)));
  const auto count = meshopt_simplifyWithAttributes(
      simplified.data(), indices.data(), indices.size(),
      positionValues->second.values.data(), vertexCount, 3 * sizeof(float),
      attributes.empty() ? nullptr : attributes.data(),
      weights.size() * sizeof(float),
      weights.empty() ? nullptr : weights.data(), weights.size(), nullptr,
      targetTriangles * 3, maxError, meshopt_SimplifyLockBorder,
      &achievedError);
  if (count == 0 || count % 3 != 0 || count > indices.size()) {
    error =
        "Static model LOD simplification produced invalid triangle coverage.";
    return false;
  }
  simplified.resize(count);
  std::vector<std::uint32_t> remap(vertexCount,
                                   std::numeric_limits<std::uint32_t>::max());
  std::vector<std::uint32_t> retained;
  retained.reserve(vertexCount);
  for (const auto index : simplified) {
    if (remap[index] == std::numeric_limits<std::uint32_t>::max()) {
      remap[index] = static_cast<std::uint32_t>(retained.size());
      retained.push_back(index);
    }
    result.indices.push_back(remap[index]);
  }
  for (auto &[name, attribute] : result.attributes) {
    std::vector<float> compact;
    compact.reserve(retained.size() * attribute.components);
    for (const auto index : retained) {
      compact.insert(compact.end(),
                     attribute.values.begin() + index * attribute.components,
                     attribute.values.begin() +
                         (index + 1) * attribute.components);
    }
    attribute.values = std::move(compact);
  }
  return true;
}
} // namespace

bool simplifyStaticModelLod(const cgltf_data &document, float ratio,
                            float maxError, ModelLodSimplification &output,
                            std::string &error) {
  ModelLodSimplification result;
  for (std::size_t mesh = 0; mesh < document.meshes_count; ++mesh) {
    for (std::size_t primitive = 0;
         primitive < document.meshes[mesh].primitives_count; ++primitive) {
      const auto &source = document.meshes[mesh].primitives[primitive];
      if (source.type != cgltf_primitive_type_triangles) {
        error = "Static model LOD generation currently requires triangle "
                "primitives.";
        return false;
      }
      gltf::ModelLodPrimitive generated;
      generated.mesh = mesh;
      generated.primitive = primitive;
      const auto *positions =
          gltf::attribute(source, cgltf_attribute_type_position);
      result.sourceTriangles += (source.indices ? source.indices->count
                                 : positions    ? positions->count
                                                : 0) /
                                3;
      float achievedError = 0;
      if (!simplifyPrimitive(source, ratio, maxError, generated, achievedError,
                             error)) {
        return false;
      }
      result.triangles += generated.indices.size() / 3;
      result.achievedError = std::max(result.achievedError, achievedError);
      result.primitives.push_back(std::move(generated));
    }
  }
  if (result.primitives.empty()) {
    error = "Static model LOD generation requires mesh primitives.";
    return false;
  }
  output = std::move(result);
  return true;
}
} // namespace demi::assets
