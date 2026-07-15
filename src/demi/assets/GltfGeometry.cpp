#include "demi/assets/GltfGeometry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>

namespace demi::assets {
namespace {

using Json = nlohmann::json;
using Matrix = std::array<float, 16>;

struct AccessorView {
  const std::vector<unsigned char> *buffer = nullptr;
  std::size_t offset = 0;
  std::size_t stride = 0;
  std::size_t count = 0;
  int componentType = 0;
  std::string type;
};

Matrix identityMatrix() {
  return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
          0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}

Matrix multiply(const Matrix &left, const Matrix &right) {
  Matrix result{};
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row)
      for (int index = 0; index < 4; ++index)
        result[column * 4 + row] +=
            left[index * 4 + row] * right[column * 4 + index];
  return result;
}

Matrix nodeMatrix(const Json &node) {
  if (const auto matrix = node.find("matrix");
      matrix != node.end() && matrix->is_array() && matrix->size() == 16) {
    Matrix result{};
    for (std::size_t index = 0; index < result.size(); ++index)
      result[index] = (*matrix)[index].get<float>();
    return result;
  }
  const auto value = [&node](const char *name, const std::size_t index,
                             const float fallback) {
    const auto field = node.find(name);
    return field != node.end() && field->is_array() && field->size() > index
               ? (*field)[index].get<float>()
               : fallback;
  };
  const float x = value("rotation", 0, 0.0F);
  const float y = value("rotation", 1, 0.0F);
  const float z = value("rotation", 2, 0.0F);
  const float w = value("rotation", 3, 1.0F);
  const float sx = value("scale", 0, 1.0F);
  const float sy = value("scale", 1, 1.0F);
  const float sz = value("scale", 2, 1.0F);
  return {
      (1.0F - 2.0F * (y * y + z * z)) * sx,
      (2.0F * (x * y + z * w)) * sx,
      (2.0F * (x * z - y * w)) * sx,
      0.0F,
      (2.0F * (x * y - z * w)) * sy,
      (1.0F - 2.0F * (x * x + z * z)) * sy,
      (2.0F * (y * z + x * w)) * sy,
      0.0F,
      (2.0F * (x * z + y * w)) * sz,
      (2.0F * (y * z - x * w)) * sz,
      (1.0F - 2.0F * (x * x + y * y)) * sz,
      0.0F,
      value("translation", 0, 0.0F),
      value("translation", 1, 0.0F),
      value("translation", 2, 0.0F),
      1.0F,
  };
}

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

bool readFile(const std::filesystem::path &path,
              std::vector<unsigned char> &out) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  out.assign(std::istreambuf_iterator<char>(input), {});
  return true;
}

std::optional<AccessorView>
accessorView(const Json &document,
             const std::vector<std::vector<unsigned char>> &buffers,
             const int index, std::string &error) {
  if (index < 0 || !document.contains("accessors") ||
      static_cast<std::size_t>(index) >= document["accessors"].size()) {
    error = "glTF references a missing accessor.";
    return std::nullopt;
  }
  const Json &accessor = document["accessors"][index];
  const int viewIndex = accessor.value("bufferView", -1);
  if (viewIndex < 0 || !document.contains("bufferViews") ||
      static_cast<std::size_t>(viewIndex) >= document["bufferViews"].size()) {
    error = "glTF accessor has no supported buffer view.";
    return std::nullopt;
  }
  const Json &view = document["bufferViews"][viewIndex];
  const int bufferIndex = view.value("buffer", -1);
  if (bufferIndex < 0 ||
      static_cast<std::size_t>(bufferIndex) >= buffers.size()) {
    error = "glTF buffer view references a missing buffer.";
    return std::nullopt;
  }
  const int componentType = accessor.value("componentType", 0);
  const std::string type = accessor.value("type", "");
  const std::size_t componentSize =
      componentType == 5121                            ? 1U
      : componentType == 5123                          ? 2U
      : componentType == 5125 || componentType == 5126 ? 4U
                                                       : 0U;
  const std::size_t components = type == "SCALAR" ? 1U
                                 : type == "VEC3" ? 3U
                                                  : 0U;
  if (componentSize == 0 || components == 0) {
    error = "glTF accessor has an unsupported component type.";
    return std::nullopt;
  }
  const std::size_t offset =
      view.value("byteOffset", 0U) + accessor.value("byteOffset", 0U);
  const std::size_t stride =
      view.value("byteStride", componentSize * components);
  const std::size_t count = accessor.value("count", 0U);
  const auto &buffer = buffers[bufferIndex];
  if (count == 0 || stride < componentSize * components ||
      offset + (count - 1U) * stride + componentSize * components >
          buffer.size()) {
    error = "glTF accessor extends beyond its buffer.";
    return std::nullopt;
  }
  return AccessorView{.buffer = &buffer,
                      .offset = offset,
                      .stride = stride,
                      .count = count,
                      .componentType = componentType,
                      .type = type};
}

template <typename Value>
Value readValue(const AccessorView &view, const std::size_t index) {
  Value value{};
  std::memcpy(&value, view.buffer->data() + view.offset + index * view.stride,
              sizeof(Value));
  return value;
}

std::optional<GltfPoint3> positionAt(const AccessorView &view,
                                     const std::size_t index) {
  if (view.componentType != 5126 || view.type != "VEC3" || index >= view.count)
    return std::nullopt;
  const unsigned char *bytes =
      view.buffer->data() + view.offset + index * view.stride;
  GltfPoint3 point;
  std::memcpy(&point.x, bytes, sizeof(float));
  std::memcpy(&point.y, bytes + sizeof(float), sizeof(float));
  std::memcpy(&point.z, bytes + sizeof(float) * 2U, sizeof(float));
  return point;
}

std::optional<std::uint32_t> indexAt(const AccessorView &view,
                                     const std::size_t index) {
  if (view.type != "SCALAR" || index >= view.count)
    return std::nullopt;
  switch (view.componentType) {
  case 5121:
    return readValue<std::uint8_t>(view, index);
  case 5123:
    return readValue<std::uint16_t>(view, index);
  case 5125:
    return readValue<std::uint32_t>(view, index);
  default:
    return std::nullopt;
  }
}

bool appendPrimitive(const Json &document,
                     const std::vector<std::vector<unsigned char>> &buffers,
                     const Json &primitive, const Matrix &transform,
                     GltfGeometry &geometry, std::string &error) {
  if (primitive.value("mode", 4) != 4)
    return true;
  const auto attributes = primitive.find("attributes");
  if (attributes == primitive.end() || !attributes->contains("POSITION"))
    return true;
  const auto positions = accessorView(
      document, buffers, (*attributes)["POSITION"].get<int>(), error);
  if (!positions)
    return false;
  std::optional<AccessorView> indices;
  if (const auto index = primitive.find("indices"); index != primitive.end()) {
    indices = accessorView(document, buffers, index->get<int>(), error);
    if (!indices)
      return false;
  }
  const std::size_t indexCount = indices ? indices->count : positions->count;
  for (std::size_t index = 0; index + 2 < indexCount; index += 3) {
    const auto resolve =
        [&](const std::size_t item) -> std::optional<GltfPoint3> {
      const auto indexed =
          indices ? indexAt(*indices, item)
                  : std::make_optional(static_cast<std::uint32_t>(item));
      if (!indexed)
        return std::nullopt;
      const std::size_t positionIndex = *indexed;
      const auto position = positionAt(*positions, positionIndex);
      return position ? std::make_optional(transformPoint(transform, *position))
                      : std::nullopt;
    };
    const auto a = resolve(index);
    const auto b = resolve(index + 1U);
    const auto c = resolve(index + 2U);
    if (!a || !b || !c) {
      error = "glTF triangle references a missing or unsupported vertex.";
      return false;
    }
    geometry.triangles.push_back({.a = *a, .b = *b, .c = *c});
    expand(geometry, *a);
    expand(geometry, *b);
    expand(geometry, *c);
  }
  return true;
}

bool visitNode(const Json &document,
               const std::vector<std::vector<unsigned char>> &buffers,
               const int nodeIndex, const Matrix &parent,
               GltfGeometry &geometry, std::string &error) {
  if (nodeIndex < 0 ||
      static_cast<std::size_t>(nodeIndex) >= document["nodes"].size()) {
    error = "glTF scene references a missing node.";
    return false;
  }
  const Json &node = document["nodes"][nodeIndex];
  const Matrix transform = multiply(parent, nodeMatrix(node));
  if (const auto meshIndex = node.find("mesh"); meshIndex != node.end()) {
    const int mesh = meshIndex->get<int>();
    if (mesh < 0 ||
        static_cast<std::size_t>(mesh) >= document["meshes"].size()) {
      error = "glTF node references a missing mesh.";
      return false;
    }
    for (const Json &primitive :
         document["meshes"][mesh].value("primitives", Json{}))
      if (!appendPrimitive(document, buffers, primitive, transform, geometry,
                           error))
        return false;
  }
  if (const auto children = node.find("children"); children != node.end())
    for (const Json &child : *children)
      if (!visitNode(document, buffers, child.get<int>(), transform, geometry,
                     error))
        return false;
  return true;
}

} // namespace

std::optional<GltfGeometry> loadGltfGeometry(const std::filesystem::path &path,
                                             std::string &error) {
  Json document;
  try {
    std::ifstream input(path);
    document = Json::parse(input);
  } catch (const Json::exception &exception) {
    error = exception.what();
    return std::nullopt;
  }
  if (!document.contains("buffers") || !document.contains("nodes") ||
      !document.contains("meshes") || !document.contains("scenes")) {
    error = "glTF requires buffers, nodes, meshes, and scenes arrays.";
    return std::nullopt;
  }
  std::vector<std::vector<unsigned char>> buffers;
  for (const Json &buffer : document["buffers"]) {
    const std::string uri = buffer.value("uri", "");
    if (uri.empty() || uri.starts_with("data:") ||
        !readFile(path.parent_path() / uri, buffers.emplace_back())) {
      error = "Could not read an external glTF buffer.";
      return std::nullopt;
    }
  }
  const int sceneIndex = document.value("scene", 0);
  if (sceneIndex < 0 ||
      static_cast<std::size_t>(sceneIndex) >= document["scenes"].size()) {
    error = "glTF default scene is missing.";
    return std::nullopt;
  }
  GltfGeometry geometry{.triangles = {},
                        .minimum = {std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max()},
                        .maximum = {std::numeric_limits<float>::lowest(),
                                    std::numeric_limits<float>::lowest(),
                                    std::numeric_limits<float>::lowest()}};
  for (const Json &node : document["scenes"][sceneIndex].value("nodes", Json{}))
    if (!visitNode(document, buffers, node.get<int>(), identityMatrix(),
                   geometry, error))
      return std::nullopt;
  if (geometry.triangles.empty()) {
    error = "glTF default scene contains no triangle geometry.";
    return std::nullopt;
  }
  return geometry;
}

} // namespace demi::assets
