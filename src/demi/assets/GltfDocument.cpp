#define CGLTF_IMPLEMENTATION
#include "demi/assets/GltfDocument.h"

#include <cstdint>
#include <fstream>
#include <mbedtls/base64.h>
#include <stdexcept>
#include <string_view>

namespace demi::assets::gltf {

std::vector<std::byte> readImage(const cgltf_image &image,
                                 const std::filesystem::path &source,
                                 std::string *mime, std::size_t maximumBytes) {
  const auto checkSize = [&](std::uintmax_t size) {
    if (size > maximumBytes) {
      throw std::runtime_error("glTF image exceeds the permitted byte limit.");
    }
  };
  if (const auto *view = image.buffer_view) {
    checkSize(view->size);
    if (view->data == nullptr &&
        (view->buffer == nullptr || view->offset > view->buffer->size ||
         view->size > view->buffer->size - view->offset)) {
      throw std::runtime_error("glTF image buffer view exceeds its buffer.");
    }
    const auto *data = cgltf_buffer_view_data(view);
    if (data == nullptr) {
      throw std::runtime_error("glTF image buffer data is unavailable.");
    }
    const auto *begin = reinterpret_cast<const std::byte *>(data);
    return {begin, begin + view->size};
  }
  if (image.uri == nullptr) {
    throw std::runtime_error("glTF image has no buffer view or URI.");
  }
  const std::string_view uri(image.uri);
  if (uri.starts_with("data:")) {
    const auto separator = uri.find(',');
    if (separator == std::string_view::npos ||
        !uri.substr(0, separator).ends_with(";base64")) {
      throw std::runtime_error("Unsupported glTF image data URI.");
    }
    const auto payload = uri.substr(separator + 1);
    if (payload.empty() || payload.size() % 4 != 0) {
      throw std::runtime_error("Invalid glTF image base64 payload.");
    }
    const auto *input = reinterpret_cast<const unsigned char *>(payload.data());
    std::size_t size = 0;
    if (mbedtls_base64_decode(nullptr, 0, &size, input, payload.size()) !=
            MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL ||
        size == 0) {
      throw std::runtime_error("Invalid glTF image base64 payload.");
    }
    checkSize(size);
    std::vector<std::byte> bytes(size);
    if (mbedtls_base64_decode(reinterpret_cast<unsigned char *>(bytes.data()),
                              bytes.size(), &size, input,
                              payload.size()) != 0) {
      throw std::runtime_error("Could not decode glTF image base64 payload.");
    }
    bytes.resize(size);
    if (mime != nullptr) {
      *mime = uri.substr(5, separator - 12);
    }
    return bytes;
  }
  std::string decoded(uri);
  decoded.resize(cgltf_decode_uri(decoded.data()));
  if (decoded.find('\0') != std::string::npos) {
    throw std::runtime_error("glTF image URI contains a NUL byte.");
  }
  std::ifstream input(source.parent_path() / decoded,
                      std::ios::binary | std::ios::ate);
  const std::streamsize size = input.tellg();
  if (!input || size < 0) {
    throw std::runtime_error("Could not read glTF image: " + decoded);
  }
  checkSize(static_cast<std::uintmax_t>(size));
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char *>(bytes.data()), size)) {
    throw std::runtime_error("Could not finish reading glTF image: " + decoded);
  }
  return bytes;
}

void DocumentDeleter::operator()(cgltf_data *document) const {
  cgltf_free(document);
}

Document parseDocument(const std::filesystem::path &path, std::string &error,
                       cgltf_result *status) {
  cgltf_options options{};
  cgltf_data *raw = nullptr;
  auto result = cgltf_parse_file(&options, path.string().c_str(), &raw);
  Document document(raw);
  if (status != nullptr) {
    *status = result;
  }
  if (result != cgltf_result_success) {
    error = "Could not parse glTF/GLB model.";
    return {};
  }
  result = cgltf_validate(document.get());
  if (status != nullptr) {
    *status = result;
  }
  if (result != cgltf_result_success) {
    error = "Invalid glTF/GLB document structure.";
    return {};
  }
  return document;
}

bool loadDocumentBuffers(cgltf_data &document,
                         const std::filesystem::path &path,
                         std::string &error) {
  cgltf_options options{};
  const auto result =
      cgltf_load_buffers(&options, &document, path.string().c_str());
  if (result != cgltf_result_success ||
      cgltf_validate(&document) != cgltf_result_success) {
    error = "Could not load or validate glTF/GLB buffers.";
    return false;
  }
  return true;
}

const cgltf_accessor *attribute(const cgltf_primitive &primitive,
                                cgltf_attribute_type type, int index) {
  for (cgltf_size item = 0; item < primitive.attributes_count; ++item) {
    const auto &attribute = primitive.attributes[item];
    if (attribute.type == type && attribute.index == index) {
      return attribute.data;
    }
  }
  return nullptr;
}

} // namespace demi::assets::gltf
