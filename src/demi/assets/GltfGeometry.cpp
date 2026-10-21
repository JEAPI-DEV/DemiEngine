#include "demi/assets/GltfGeometry.h"
#include "demi/assets/GltfDocument.h"

#include <algorithm>
#include <array>
#include <limits>

namespace demi::assets {
namespace {

using Matrix = std::array<float, 16>;

GltfPoint3 transformPoint(const Matrix &matrix, const GltfPoint3 point) {
  return {.x = matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z +
               matrix[12],
          .y = matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z +
               matrix[13],
          .z = matrix[2] * point.x + matrix[6] * point.y +
               matrix[10] * point.z + matrix[14]};
}

void expand(GltfGeometry &geometry, const GltfPoint3 point) {
  geometry.minimum.x = std::min(geometry.minimum.x, point.x);
  geometry.minimum.y = std::min(geometry.minimum.y, point.y);
  geometry.minimum.z = std::min(geometry.minimum.z, point.z);
  geometry.maximum.x = std::max(geometry.maximum.x, point.x);
  geometry.maximum.y = std::max(geometry.maximum.y, point.y);
  geometry.maximum.z = std::max(geometry.maximum.z, point.z);
}

bool appendPrimitive(const cgltf_primitive &primitive, const Matrix &transform,
                     const Matrix &conversion, GltfGeometry &geometry,
                     std::string &error) {
  if (primitive.type != cgltf_primitive_type_triangles) {
    return true;
  }
  const auto *positions =
      gltf::attribute(primitive, cgltf_attribute_type_position);
  if (positions == nullptr) {
    return true;
  }
  const auto *textureCoordinates =
      gltf::attribute(primitive, cgltf_attribute_type_texcoord);
  const auto *indices = primitive.indices;
  const cgltf_size indexCount = indices ? indices->count : positions->count;
  for (cgltf_size index = 0; index + 2 < indexCount; index += 3) {
    struct Vertex {
      GltfPoint3 position;
      GltfPoint2 uv;
    };
    const auto resolve = [&](cgltf_size item, Vertex &vertex) {
      const cgltf_size positionIndex =
          indices ? cgltf_accessor_read_index(indices, item) : item;
      float position[3]{};
      if (positionIndex >= positions->count ||
          positions->type != cgltf_type_vec3 ||
          !cgltf_accessor_read_float(positions, positionIndex, position, 3)) {
        return false;
      }
      vertex.position = transformPoint(
          conversion,
          transformPoint(transform, {position[0], position[1], position[2]}));
      if (textureCoordinates != nullptr) {
        float uv[2]{};
        if (positionIndex >= textureCoordinates->count ||
            textureCoordinates->type != cgltf_type_vec2 ||
            !cgltf_accessor_read_float(textureCoordinates, positionIndex, uv,
                                       2)) {
          return false;
        }
        vertex.uv = {uv[0], uv[1]};
      }
      return true;
    };
    Vertex a{};
    Vertex b{};
    Vertex c{};
    if (!resolve(index, a) || !resolve(index + 1, b) ||
        !resolve(index + 2, c)) {
      error = "glTF triangle references a missing or unsupported vertex.";
      return false;
    }
    geometry.triangles.push_back({.a = a.position,
                                  .b = b.position,
                                  .c = c.position,
                                  .uvA = a.uv,
                                  .uvB = b.uv,
                                  .uvC = c.uv});
    expand(geometry, a.position);
    expand(geometry, b.position);
    expand(geometry, c.position);
  }
  return true;
}

bool visitNode(const cgltf_node &node, const Matrix &conversion,
               GltfGeometry &geometry, std::string &error) {
  Matrix transform{};
  cgltf_node_transform_world(&node, transform.data());
  if (node.mesh != nullptr) {
    for (cgltf_size index = 0; index < node.mesh->primitives_count; ++index) {
      if (!appendPrimitive(node.mesh->primitives[index], transform, conversion,
                           geometry, error)) {
        return false;
      }
    }
  }
  for (cgltf_size index = 0; index < node.children_count; ++index) {
    if (!visitNode(*node.children[index], conversion, geometry, error)) {
      return false;
    }
  }
  return true;
}

} // namespace

std::optional<GltfGeometry> loadGltfGeometry(const std::filesystem::path &path,
                                             std::string &error) {
  return loadGltfGeometry(path, ModelImportProfile{}, error);
}

std::optional<GltfGeometry> loadGltfGeometry(const std::filesystem::path &path,
                                             const ModelImportProfile &profile,
                                             std::string &error) {
  auto document = gltf::parseDocument(path, error);
  if (!document) {
    return std::nullopt;
  }
  const cgltf_scene *scene = document->scene;
  if (scene == nullptr && document->scenes_count > 0) {
    scene = &document->scenes[0];
  }
  if (scene == nullptr) {
    error = "glTF default scene is missing.";
    return std::nullopt;
  }
  if (!gltf::loadDocumentBuffers(*document, path, error)) {
    return std::nullopt;
  }
  GltfGeometry geometry{.triangles = {},
                        .minimum = {std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max()},
                        .maximum = {std::numeric_limits<float>::lowest(),
                                    std::numeric_limits<float>::lowest(),
                                    std::numeric_limits<float>::lowest()}};
  const Matrix conversion = modelImportConversion(profile);
  for (cgltf_size index = 0; index < scene->nodes_count; ++index) {
    if (!visitNode(*scene->nodes[index], conversion, geometry, error)) {
      return std::nullopt;
    }
  }
  if (geometry.triangles.empty()) {
    error = "glTF default scene contains no triangle geometry.";
    return std::nullopt;
  }
  return geometry;
}

} // namespace demi::assets
