#define CGLTF_IMPLEMENTATION
#include "demi/assets/GltfDocument.h"

namespace demi::assets::gltf {

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
