#include "demi/assets/ModelLodGltfWriter.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace demi::assets::gltf {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maximumSize = std::numeric_limits<std::uint32_t>::max();

void checkAppend(std::size_t current, std::size_t additional) {
  if (additional > maximumSize || current > maximumSize - additional) {
    throw std::runtime_error("GLB exceeds its 32-bit size limit.");
  }
}

void appendU32(std::vector<std::byte> &bytes, std::uint32_t value) {
  checkAppend(bytes.size(), 4);
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xff));
  }
}

void alignBinary(std::vector<std::byte> &bytes) {
  while (bytes.size() % 4 != 0) {
    checkAppend(bytes.size(), 1);
    bytes.push_back(std::byte{0});
  }
}

std::size_t appendBytes(std::vector<std::byte> &binary, const void *data,
                        std::size_t size) {
  alignBinary(binary);
  checkAppend(binary.size(), size);
  const auto offset = binary.size();
  if (size != 0) {
    if (data == nullptr) {
      throw std::runtime_error("glTF buffer data was not loaded.");
    }
    const auto *begin = static_cast<const std::byte *>(data);
    binary.insert(binary.end(), begin, begin + size);
  }
  return offset;
}

std::size_t appendView(Json &root, std::size_t offset, std::size_t size,
                       int target = 0) {
  auto &views = root["bufferViews"];
  if (views.is_null()) {
    views = Json::array();
  }
  Json view = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", size}};
  if (target != 0) {
    view["target"] = target;
  }
  const auto index = views.size();
  views.push_back(std::move(view));
  return index;
}

std::vector<std::byte> readImageUri(std::string_view uri,
                                    const std::filesystem::path &source,
                                    std::string &mime) {
  if (uri.starts_with("data:")) {
    const auto separator = uri.find(',');
    if (separator == std::string_view::npos ||
        !uri.substr(0, separator).ends_with(";base64")) {
      throw std::runtime_error("Unsupported glTF image data URI.");
    }
    mime = std::string(uri.substr(5, separator - 5 - 7));
    const auto payload = uri.substr(separator + 1);
    if (payload.empty() || payload.size() % 4 != 0) {
      throw std::runtime_error("Invalid glTF image base64 payload.");
    }
    auto size = payload.size() / 4 * 3;
    if (payload.ends_with("==")) {
      size -= 2;
    } else if (payload.ends_with("=")) {
      --size;
    }
    checkAppend(0, size);
    cgltf_options options{};
    void *data = nullptr;
    if (cgltf_load_buffer_base64(&options, size, payload.data(), &data) !=
        cgltf_result_success) {
      throw std::runtime_error("Could not decode glTF image base64 payload.");
    }
    const std::unique_ptr<void, decltype(&std::free)> allocation(data,
                                                                 &std::free);
    const auto *begin = static_cast<const std::byte *>(data);
    return {begin, begin + size};
  }
  std::string decoded(uri);
  decoded.resize(cgltf_decode_uri(decoded.data()));
  std::ifstream input(source.parent_path() / decoded, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Could not read glTF image: " + decoded);
  }
  const std::vector<char> contents((std::istreambuf_iterator<char>(input)), {});
  if (input.bad()) {
    throw std::runtime_error("Could not finish reading glTF image: " + decoded);
  }
  checkAppend(0, contents.size());
  std::vector<std::byte> bytes(contents.size());
  std::transform(
      contents.begin(), contents.end(), bytes.begin(), [](char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
      });
  return bytes;
}

std::string imageMime(const std::vector<std::byte> &bytes) {
  if (bytes.size() >= 8 && bytes[0] == std::byte{0x89} &&
      bytes[1] == std::byte{'P'} && bytes[2] == std::byte{'N'} &&
      bytes[3] == std::byte{'G'}) {
    return "image/png";
  }
  if (bytes.size() >= 3 && bytes[0] == std::byte{0xff} &&
      bytes[1] == std::byte{0xd8} && bytes[2] == std::byte{0xff}) {
    return "image/jpeg";
  }
  if (bytes.size() >= 12 && bytes[0] == std::byte{'R'} &&
      bytes[8] == std::byte{'W'} && bytes[9] == std::byte{'E'} &&
      bytes[10] == std::byte{'B'} && bytes[11] == std::byte{'P'}) {
    return "image/webp";
  }
  if (bytes.size() >= 12 && bytes[0] == std::byte{0xab} &&
      bytes[1] == std::byte{'K'} && bytes[2] == std::byte{'T'} &&
      bytes[3] == std::byte{'X'}) {
    return "image/ktx2";
  }
  throw std::runtime_error("Cannot determine embedded glTF image MIME type.");
}

void embedBuffers(const cgltf_data &document, Json &root,
                  std::vector<std::byte> &binary) {
  std::vector<std::size_t> offsets;
  for (cgltf_size i = 0; i < document.buffers_count; ++i) {
    const auto &buffer = document.buffers[i];
    offsets.push_back(appendBytes(binary, buffer.data, buffer.size));
  }
  if (!root.contains("bufferViews")) {
    return;
  }
  for (auto &view : root.at("bufferViews")) {
    const auto buffer = view.at("buffer").get<std::size_t>();
    const auto offset = view.value("byteOffset", std::size_t{0});
    const auto length = view.at("byteLength").get<std::size_t>();
    if (buffer >= offsets.size() || offset > document.buffers[buffer].size ||
        length > document.buffers[buffer].size - offset) {
      throw std::runtime_error("glTF buffer view exceeds its source buffer.");
    }
    view["buffer"] = 0;
    view["byteOffset"] = offsets[buffer] + offset;
    // Compressed buffers have their own buffer-relative range. Leaving that
    // range unchanged would silently corrupt an otherwise preserved extension.
    if (view.contains("extensions") &&
        view["extensions"].contains("EXT_meshopt_compression")) {
      auto &compressed = view["extensions"]["EXT_meshopt_compression"];
      const auto compressedBuffer = compressed.at("buffer").get<std::size_t>();
      const auto compressedOffset =
          compressed.value("byteOffset", std::size_t{0});
      const auto compressedLength =
          compressed.at("byteLength").get<std::size_t>();
      if (compressedBuffer >= offsets.size() ||
          compressedOffset > document.buffers[compressedBuffer].size ||
          compressedLength >
              document.buffers[compressedBuffer].size - compressedOffset) {
        throw std::runtime_error(
            "Compressed glTF buffer view exceeds its buffer.");
      }
      compressed["buffer"] = 0;
      compressed["byteOffset"] = offsets[compressedBuffer] + compressedOffset;
    }
  }
}

void embedImages(Json &root, const std::filesystem::path &source,
                 std::vector<std::byte> &binary) {
  if (!root.contains("images")) {
    return;
  }
  for (auto &image : root.at("images")) {
    if (!image.contains("uri")) {
      continue;
    }
    std::string mime = image.value("mimeType", std::string{});
    auto bytes = readImageUri(image.at("uri").get<std::string>(), source, mime);
    if (mime.empty()) {
      mime = imageMime(bytes);
    }
    const auto offset = appendBytes(binary, bytes.data(), bytes.size());
    image["bufferView"] = appendView(root, offset, bytes.size());
    image["mimeType"] = mime;
    image.erase("uri");
  }
}

std::size_t appendAttribute(Json &root, std::vector<std::byte> &binary,
                            const std::string &name,
                            const ModelLodAttribute &attribute) {
  const std::map<std::string, std::size_t> components = {
      {"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4},
      {"MAT2", 4},   {"MAT3", 9}, {"MAT4", 16}};
  const auto type = components.find(attribute.type);
  if (type == components.end() || attribute.components != type->second ||
      attribute.values.empty() ||
      attribute.values.size() % attribute.components != 0) {
    throw std::runtime_error("Invalid generated glTF attribute: " + name);
  }
  alignBinary(binary);
  const auto offset = binary.size();
  for (const auto value : attribute.values) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("Nonfinite generated glTF attribute: " + name);
    }
    appendU32(binary, std::bit_cast<std::uint32_t>(value));
  }
  Json accessor = {
      {"bufferView", appendView(root, offset, binary.size() - offset, 34962)},
      {"componentType", 5126},
      {"count", attribute.values.size() / attribute.components},
      {"type", attribute.type}};
  if (name == "POSITION") {
    if (attribute.type != "VEC3") {
      throw std::runtime_error("Generated POSITION must be VEC3.");
    }
    std::vector<float> minimum(3, std::numeric_limits<float>::max());
    std::vector<float> maximum(3, std::numeric_limits<float>::lowest());
    for (std::size_t i = 0; i < attribute.values.size(); ++i) {
      minimum[i % 3] = std::min(minimum[i % 3], attribute.values[i]);
      maximum[i % 3] = std::max(maximum[i % 3], attribute.values[i]);
    }
    accessor["min"] = minimum;
    accessor["max"] = maximum;
  }
  auto &accessors = root["accessors"];
  if (accessors.is_null()) {
    accessors = Json::array();
  }
  const auto index = accessors.size();
  accessors.push_back(std::move(accessor));
  return index;
}

void replacePrimitive(Json &root, std::vector<std::byte> &binary,
                      const ModelLodPrimitive &replacement) {
  auto &primitive = root.at("meshes")
                        .at(replacement.mesh)
                        .at("primitives")
                        .at(replacement.primitive);
  if (replacement.indices.empty() || replacement.indices.size() % 3 != 0 ||
      !replacement.attributes.contains("POSITION")) {
    throw std::runtime_error(
        "Generated glTF primitive needs positions and triangle indices.");
  }
  const auto &positions = replacement.attributes.at("POSITION");
  const auto count = positions.values.size() / 3;
  Json attributes = Json::object();
  for (const auto &[name, attribute] : replacement.attributes) {
    if (attribute.components == 0 ||
        attribute.values.size() / attribute.components != count) {
      throw std::runtime_error(
          "Generated glTF attributes have inconsistent vertex counts.");
    }
    attributes[name] = appendAttribute(root, binary, name, attribute);
  }
  alignBinary(binary);
  const auto offset = binary.size();
  for (const auto index : replacement.indices) {
    if (index >= count) {
      throw std::runtime_error(
          "Generated glTF index exceeds its vertex count.");
    }
    appendU32(binary, index);
  }
  auto &accessors = root["accessors"];
  const auto accessor = accessors.size();
  accessors.push_back(
      {{"bufferView", appendView(root, offset, binary.size() - offset, 34963)},
       {"componentType", 5125},
       {"count", replacement.indices.size()},
       {"type", "SCALAR"}});
  primitive["attributes"] = std::move(attributes);
  primitive["indices"] = accessor;
  primitive["mode"] = 4;
  // Compression extensions describe the original geometry, not the new arrays.
  if (primitive.contains("extensions")) {
    primitive["extensions"].erase("KHR_draco_mesh_compression");
    if (primitive["extensions"].empty()) {
      primitive.erase("extensions");
    }
  }
}
} // namespace

bool writeModelLodGlb(const cgltf_data &document,
                      const std::filesystem::path &source,
                      const std::vector<ModelLodPrimitive> &primitives,
                      std::vector<std::byte> &output, std::string &error) {
  try {
    if (document.json == nullptr || document.json_size == 0) {
      throw std::runtime_error("glTF source JSON is unavailable.");
    }
    Json root = Json::parse(document.json, document.json + document.json_size);
    std::vector<std::byte> binary;
    embedBuffers(document, root, binary);
    embedImages(root, source, binary);
    std::set<std::pair<std::size_t, std::size_t>> replaced;
    for (const auto &primitive : primitives) {
      if (!replaced.emplace(primitive.mesh, primitive.primitive).second) {
        throw std::runtime_error(
            "Duplicate generated glTF primitive replacement.");
      }
      replacePrimitive(root, binary, primitive);
    }
    root["buffers"] = Json::array({{{"byteLength", binary.size()}}});
    auto json = root.dump();
    while (json.size() % 4 != 0) {
      json.push_back(' ');
    }
    alignBinary(binary);
    checkAppend(28, json.size());
    checkAppend(28 + json.size(), binary.size());
    std::vector<std::byte> result;
    appendU32(result, 0x46546c67);
    appendU32(result, 2);
    appendU32(result,
              static_cast<std::uint32_t>(28 + json.size() + binary.size()));
    appendU32(result, static_cast<std::uint32_t>(json.size()));
    appendU32(result, 0x4e4f534a);
    appendBytes(result, json.data(), json.size());
    appendU32(result, static_cast<std::uint32_t>(binary.size()));
    appendU32(result, 0x004e4942);
    appendBytes(result, binary.data(), binary.size());
    output = std::move(result);
    error.clear();
    return true;
  } catch (const std::exception &exception) {
    error =
        "Could not serialize model LOD GLB: " + std::string(exception.what());
    return false;
  }
}
} // namespace demi::assets::gltf
