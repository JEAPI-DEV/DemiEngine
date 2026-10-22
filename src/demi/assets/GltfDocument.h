#pragma once

// Keep our bundled cgltf symbols distinct from bgfx's own implementation.
#define cgltf_accessor_index demi_cgltf_accessor_index
#define cgltf_accessor_read_float demi_cgltf_accessor_read_float
#define cgltf_accessor_read_index demi_cgltf_accessor_read_index
#define cgltf_accessor_read_uint demi_cgltf_accessor_read_uint
#define cgltf_accessor_unpack_floats demi_cgltf_accessor_unpack_floats
#define cgltf_accessor_unpack_indices demi_cgltf_accessor_unpack_indices
#define cgltf_animation_channel_index demi_cgltf_animation_channel_index
#define cgltf_animation_index demi_cgltf_animation_index
#define cgltf_animation_sampler_index demi_cgltf_animation_sampler_index
#define cgltf_buffer_index demi_cgltf_buffer_index
#define cgltf_buffer_view_data demi_cgltf_buffer_view_data
#define cgltf_buffer_view_index demi_cgltf_buffer_view_index
#define cgltf_calc_size demi_cgltf_calc_size
#define cgltf_camera_index demi_cgltf_camera_index
#define cgltf_component_size demi_cgltf_component_size
#define cgltf_copy_extras_json demi_cgltf_copy_extras_json
#define cgltf_decode_string demi_cgltf_decode_string
#define cgltf_decode_uri demi_cgltf_decode_uri
#define cgltf_find_accessor demi_cgltf_find_accessor
#define cgltf_free demi_cgltf_free
#define cgltf_image_index demi_cgltf_image_index
#define cgltf_light_index demi_cgltf_light_index
#define cgltf_load_buffer_base64 demi_cgltf_load_buffer_base64
#define cgltf_load_buffers demi_cgltf_load_buffers
#define cgltf_material_index demi_cgltf_material_index
#define cgltf_mesh_index demi_cgltf_mesh_index
#define cgltf_node_index demi_cgltf_node_index
#define cgltf_node_transform_local demi_cgltf_node_transform_local
#define cgltf_node_transform_world demi_cgltf_node_transform_world
#define cgltf_num_components demi_cgltf_num_components
#define cgltf_parse demi_cgltf_parse
#define cgltf_parse_file demi_cgltf_parse_file
#define cgltf_sampler_index demi_cgltf_sampler_index
#define cgltf_scene_index demi_cgltf_scene_index
#define cgltf_skin_index demi_cgltf_skin_index
#define cgltf_texture_index demi_cgltf_texture_index
#define cgltf_validate demi_cgltf_validate
#include <cgltf.h>

#include <filesystem>
#include <memory>
#include <string>

namespace demi::assets::gltf {

struct DocumentDeleter {
  void operator()(cgltf_data *document) const;
};
using Document = std::unique_ptr<cgltf_data, DocumentDeleter>;

// Metadata-only loading keeps accessor-bound collider generation inexpensive.
[[nodiscard]] Document parseDocument(const std::filesystem::path &path,
                                     std::string &error,
                                     cgltf_result *status = nullptr);
[[nodiscard]] bool loadDocumentBuffers(cgltf_data &document,
                                       const std::filesystem::path &path,
                                       std::string &error);
[[nodiscard]] const cgltf_accessor *attribute(const cgltf_primitive &primitive,
                                              cgltf_attribute_type type,
                                              int index = 0);

} // namespace demi::assets::gltf
