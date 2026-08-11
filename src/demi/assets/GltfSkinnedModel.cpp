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
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "demi/assets/GltfSkinnedModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>

namespace demi::assets {
namespace {

using Matrix = std::array<float, 16>;

Matrix identity() {
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

runtime::Vec3 transformPoint(const Matrix &matrix, const runtime::Vec3 point) {
  return {.x = matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z +
               matrix[12],
          .y = matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z +
               matrix[13],
          .z = matrix[2] * point.x + matrix[6] * point.y +
               matrix[10] * point.z + matrix[14]};
}

Matrix trs(const runtime::Vec3 translation, std::array<float, 4> rotation,
           const runtime::Vec3 scale) {
  const float length =
      std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                rotation[2] * rotation[2] + rotation[3] * rotation[3]);
  if (length > 0.000001F)
    for (float &value : rotation)
      value /= length;
  const float x = rotation[0];
  const float y = rotation[1];
  const float z = rotation[2];
  const float w = rotation[3];
  return {(1.0F - 2.0F * (y * y + z * z)) * scale.x,
          (2.0F * (x * y + z * w)) * scale.x,
          (2.0F * (x * z - y * w)) * scale.x,
          0.0F,
          (2.0F * (x * y - z * w)) * scale.y,
          (1.0F - 2.0F * (x * x + z * z)) * scale.y,
          (2.0F * (y * z + x * w)) * scale.y,
          0.0F,
          (2.0F * (x * z + y * w)) * scale.z,
          (2.0F * (y * z - x * w)) * scale.z,
          (1.0F - 2.0F * (x * x + y * y)) * scale.z,
          0.0F,
          translation.x,
          translation.y,
          translation.z,
          1.0F};
}

int nodeIndex(const cgltf_data &data, const cgltf_node *node) {
  return node == nullptr ? -1 : static_cast<int>(node - data.nodes);
}

int skinIndex(const cgltf_data &data, const cgltf_skin *skin) {
  return skin == nullptr ? -1 : static_cast<int>(skin - data.skins);
}

const cgltf_accessor *attribute(const cgltf_primitive &primitive,
                                const cgltf_attribute_type type,
                                const int index = 0) {
  for (cgltf_size item = 0; item < primitive.attributes_count; ++item)
    if (primitive.attributes[item].type == type &&
        primitive.attributes[item].index == index)
      return primitive.attributes[item].data;
  return nullptr;
}

bool readVec(const cgltf_accessor *accessor, const cgltf_size index,
             float *values, const cgltf_size count) {
  return accessor != nullptr &&
         cgltf_accessor_read_float(accessor, index, values, count);
}

std::array<float, 4> sampleChannel(const GltfSkinnedModel3D::Channel &channel,
                                   const float time) {
  if (channel.times.empty())
    return {};
  const auto upper =
      std::upper_bound(channel.times.begin(), channel.times.end(), time);
  if (upper == channel.times.begin())
    return channel.values.front();
  if (upper == channel.times.end())
    return channel.values.back();
  const std::size_t right =
      static_cast<std::size_t>(std::distance(channel.times.begin(), upper));
  const std::size_t left = right - 1U;
  if (channel.step)
    return channel.values[left];
  const float duration = channel.times[right] - channel.times[left];
  const float t =
      duration > 0.0F ? (time - channel.times[left]) / duration : 0.0F;
  std::array<float, 4> result{};
  std::array<float, 4> rightValue = channel.values[right];
  if (channel.path == GltfSkinnedModel3D::ChannelPath::Rotation) {
    const float dot = std::inner_product(channel.values[left].begin(),
                                         channel.values[left].end(),
                                         rightValue.begin(), 0.0F);
    if (dot < 0.0F)
      for (float &component : rightValue)
        component = -component;
  }
  for (std::size_t component = 0; component < result.size(); ++component)
    result[component] =
        channel.values[left][component] +
        (rightValue[component] - channel.values[left][component]) *
            t;
  if (channel.path == GltfSkinnedModel3D::ChannelPath::Rotation) {
    const float length = std::sqrt(
        std::inner_product(result.begin(), result.end(), result.begin(), 0.0F));
    if (length > 0.000001F)
      for (float &value : result)
        value /= length;
  }
  return result;
}

bool resolvePosePositions(
    const GltfSkinnedModel3D &model,
    const std::vector<runtime::Vec3> &translations,
    const std::vector<std::array<float, 4>> &rotations,
    const std::vector<runtime::Vec3> &scales,
    const std::vector<bool> &animated, std::vector<runtime::Vec3> &out,
    std::string &error) {
  if (translations.size() != model.nodes.size() ||
      rotations.size() != model.nodes.size() ||
      scales.size() != model.nodes.size() ||
      animated.size() != model.nodes.size()) {
    error = "GLB pose data does not match its node count.";
    return false;
  }

  std::vector<Matrix> globals(model.nodes.size());
  // 0 = unresolved, 1 = resolving, 2 = resolved. The middle state catches
  // malformed parent cycles before they recurse indefinitely.
  std::vector<std::uint8_t> state(model.nodes.size(), 0U);
  const auto resolve = [&](const auto &self, const int index) -> bool {
    if (index < 0 || static_cast<std::size_t>(index) >= model.nodes.size()) {
      error = "GLB node references an invalid parent index.";
      return false;
    }
    if (state[index] == 2U)
      return true;
    if (state[index] == 1U) {
      error = "GLB node hierarchy contains a cycle.";
      return false;
    }
    state[index] = 1U;
    const GltfSkinnedModel3D::Node &node = model.nodes[index];
    const Matrix local =
        node.matrixAuthored && !animated[index]
            ? node.localMatrix
            : trs(translations[index], rotations[index], scales[index]);
    if (node.parent >= 0) {
      if (!self(self, node.parent))
        return false;
      globals[index] = multiply(globals[node.parent], local);
    } else {
      globals[index] = local;
    }
    state[index] = 2U;
    return true;
  };
  for (std::size_t index = 0; index < model.nodes.size(); ++index)
    if (!resolve(resolve, static_cast<int>(index)))
      return false;

  std::vector<std::vector<Matrix>> palettes;
  palettes.reserve(model.skins.size());
  for (const GltfSkinnedModel3D::Skin &skin : model.skins) {
    if (skin.joints.size() != skin.inverseBindMatrices.size()) {
      error = "GLB skin joint and inverse-bind counts do not match.";
      return false;
    }
    std::vector<Matrix> palette;
    palette.reserve(skin.joints.size());
    for (std::size_t joint = 0; joint < skin.joints.size(); ++joint) {
      const int node = skin.joints[joint];
      if (node < 0 || static_cast<std::size_t>(node) >= globals.size()) {
        error = "GLB skin references an invalid joint node.";
        return false;
      }
      palette.push_back(
          multiply(globals[node], skin.inverseBindMatrices[joint]));
    }
    palettes.push_back(std::move(palette));
  }

  out.resize(model.vertices.size());
  for (std::size_t index = 0; index < model.vertices.size(); ++index) {
    const GltfSkinnedVertex3D &vertex = model.vertices[index];
    if (vertex.skin < 0) {
      if (vertex.node >= 0) {
        if (static_cast<std::size_t>(vertex.node) >= globals.size()) {
          error = "GLB vertex references an invalid owner node.";
          return false;
        }
        out[index] = transformPoint(globals[vertex.node], vertex.position);
      } else {
        out[index] = vertex.position;
      }
      continue;
    }
    if (static_cast<std::size_t>(vertex.skin) >= palettes.size()) {
      error = "GLB vertex references an invalid skin.";
      return false;
    }
    runtime::Vec3 position{};
    float totalWeight = 0.0F;
    for (std::size_t influence = 0; influence < 4; ++influence) {
      const float weight = vertex.weights[influence];
      const std::size_t joint = vertex.joints[influence];
      if (weight <= 0.0F)
        continue;
      if (joint >= palettes[vertex.skin].size()) {
        error = "GLB vertex references an invalid skin joint.";
        return false;
      }
      const runtime::Vec3 transformed =
          transformPoint(palettes[vertex.skin][joint], vertex.position);
      position.x += transformed.x * weight;
      position.y += transformed.y * weight;
      position.z += transformed.z * weight;
      totalWeight += weight;
    }
    out[index] = totalWeight > 0.0F ? position : vertex.position;
  }
  for (runtime::Vec3 &position : out)
    position = transformPoint(model.importTransform, position);
  error.clear();
  return true;
}

} // namespace

int GltfSkinnedModel3D::clipIndex(const std::string_view name,
                                  const int fallback) const {
  if (!name.empty())
    for (std::size_t index = 0; index < clips.size(); ++index)
      if (clips[index].name == name)
        return static_cast<int>(index);
  return clips.empty()
             ? -1
             : std::clamp(fallback, 0, static_cast<int>(clips.size() - 1U));
}

bool GltfSkinnedModel3D::bindPosePositions(
    std::vector<runtime::Vec3> &out, std::string &error) const {
  std::vector<runtime::Vec3> translations;
  std::vector<std::array<float, 4>> rotations;
  std::vector<runtime::Vec3> scales;
  translations.reserve(nodes.size());
  rotations.reserve(nodes.size());
  scales.reserve(nodes.size());
  for (const Node &node : nodes) {
    translations.push_back(node.translation);
    rotations.push_back(node.rotation);
    scales.push_back(node.scale);
  }
  return resolvePosePositions(*this, translations, rotations, scales,
                              std::vector<bool>(nodes.size(), false), out,
                              error);
}

bool GltfSkinnedModel3D::samplePositions(const int clipIndex, float time,
                                         const bool loop,
                                         std::vector<runtime::Vec3> &out,
                                         std::string &error) const {
  if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= clips.size()) {
    error = "GLB animation clip index is out of range.";
    return false;
  }
  const Clip &clip = clips[clipIndex];
  if (clip.duration > 0.0F)
    time = loop ? std::fmod(std::max(time, 0.0F), clip.duration)
                : std::clamp(time, 0.0F, clip.duration);

  std::vector<runtime::Vec3> translations;
  std::vector<std::array<float, 4>> rotations;
  std::vector<runtime::Vec3> scales;
  translations.reserve(nodes.size());
  rotations.reserve(nodes.size());
  scales.reserve(nodes.size());
  for (const Node &node : nodes) {
    translations.push_back(node.translation);
    rotations.push_back(node.rotation);
    scales.push_back(node.scale);
  }
  std::vector<bool> animated(nodes.size(), false);
  for (const Channel &channel : clip.channels) {
    if (channel.node < 0 ||
        static_cast<std::size_t>(channel.node) >= nodes.size())
      continue;
    const auto value = sampleChannel(channel, time);
    animated[channel.node] = true;
    if (channel.path == ChannelPath::Translation)
      translations[channel.node] = {value[0], value[1], value[2]};
    else if (channel.path == ChannelPath::Scale)
      scales[channel.node] = {value[0], value[1], value[2]};
    else
      rotations[channel.node] = value;
  }

  return resolvePosePositions(*this, translations, rotations, scales, animated,
                              out, error);
}

std::optional<GltfSkinnedModel3D>
loadGltfSkinnedModel3D(const std::filesystem::path &path, std::string &error) {
  // The unprofiled overload predates import profiles and historically loaded
  // clips. Keep that contract for low-level callers while manifests select a
  // versioned preset explicitly.
  return loadGltfSkinnedModel3D(
      path, modelImportPreset("animated_character"), error);
}

std::optional<GltfSkinnedModel3D>
loadGltfSkinnedModel3D(const std::filesystem::path &path,
                       const ModelImportProfile &profile, std::string &error) {
  cgltf_options options{};
  cgltf_data *raw = nullptr;
  cgltf_result result = cgltf_parse_file(&options, path.string().c_str(), &raw);
  if (result != cgltf_result_success) {
    error = "Could not parse glTF/GLB model.";
    return std::nullopt;
  }
  const auto cleanup = [&] { cgltf_free(raw); };
  result = cgltf_load_buffers(&options, raw, path.string().c_str());
  if (result != cgltf_result_success ||
      cgltf_validate(raw) != cgltf_result_success) {
    error = "Could not load or validate glTF/GLB buffers.";
    cleanup();
    return std::nullopt;
  }

  GltfSkinnedModel3D model;
  model.importTransform = modelImportConversion(profile);
  model.nodes.reserve(raw->nodes_count);
  for (cgltf_size index = 0; index < raw->nodes_count; ++index) {
    const cgltf_node &source = raw->nodes[index];
    GltfSkinnedModel3D::Node node;
    cgltf_node_transform_local(&source, node.localMatrix.data());
    node.translation =
        source.has_translation
            ? runtime::Vec3{source.translation[0], source.translation[1],
                            source.translation[2]}
            : runtime::Vec3{};
    if (source.has_rotation)
      std::copy_n(source.rotation, 4, node.rotation.begin());
    node.scale =
        source.has_scale
            ? runtime::Vec3{source.scale[0], source.scale[1], source.scale[2]}
            : runtime::Vec3{1.0F, 1.0F, 1.0F};
    node.parent = nodeIndex(*raw, source.parent);
    node.matrixAuthored = source.has_matrix;
    model.nodes.push_back(node);
  }

  model.skins.reserve(raw->skins_count);
  for (cgltf_size index = 0; index < raw->skins_count; ++index) {
    const cgltf_skin &source = raw->skins[index];
    GltfSkinnedModel3D::Skin skin;
    skin.joints.reserve(source.joints_count);
    skin.inverseBindMatrices.reserve(source.joints_count);
    for (cgltf_size joint = 0; joint < source.joints_count; ++joint) {
      skin.joints.push_back(nodeIndex(*raw, source.joints[joint]));
      Matrix inverse = identity();
      if (source.inverse_bind_matrices != nullptr)
        cgltf_accessor_read_float(source.inverse_bind_matrices, joint,
                                  inverse.data(), 16);
      skin.inverseBindMatrices.push_back(inverse);
    }
    model.skins.push_back(std::move(skin));
  }

  for (cgltf_size node = 0; node < raw->nodes_count; ++node) {
    const cgltf_node &owner = raw->nodes[node];
    if (owner.mesh == nullptr)
      continue;
    const int ownerSkin = skinIndex(*raw, owner.skin);
    for (cgltf_size primitiveIndex = 0;
         primitiveIndex < owner.mesh->primitives_count; ++primitiveIndex) {
      const cgltf_primitive &primitive = owner.mesh->primitives[primitiveIndex];
      if (primitive.type != cgltf_primitive_type_triangles)
        continue;
      if (model.albedoImage.empty() && primitive.material != nullptr &&
          primitive.material->has_pbr_metallic_roughness) {
        const cgltf_texture *texture = primitive.material
                                           ->pbr_metallic_roughness
                                           .base_color_texture.texture;
        const cgltf_image *image = texture == nullptr ? nullptr : texture->image;
        if (image != nullptr && image->buffer_view != nullptr) {
          const std::uint8_t *bytes = cgltf_buffer_view_data(image->buffer_view);
          if (bytes != nullptr)
            model.albedoImage.assign(
                reinterpret_cast<const std::byte *>(bytes),
                reinterpret_cast<const std::byte *>(
                    bytes + image->buffer_view->size));
        } else if (image != nullptr && image->uri != nullptr &&
                   !std::string_view(image->uri).starts_with("data:")) {
          std::ifstream input(path.parent_path() / image->uri,
                              std::ios::binary);
          const std::vector<char> encoded((std::istreambuf_iterator<char>(input)),
                                          {});
          model.albedoImage.resize(encoded.size());
          std::transform(encoded.begin(), encoded.end(),
                         model.albedoImage.begin(), [](const char value) {
                           return static_cast<std::byte>(
                               static_cast<unsigned char>(value));
                         });
        }
      }
      const cgltf_accessor *positions =
          attribute(primitive, cgltf_attribute_type_position);
      if (positions == nullptr)
        continue;
      const cgltf_accessor *texcoords =
          attribute(primitive, cgltf_attribute_type_texcoord);
      const cgltf_accessor *joints =
          attribute(primitive, cgltf_attribute_type_joints);
      const cgltf_accessor *weights =
          attribute(primitive, cgltf_attribute_type_weights);
      runtime::Color baseColor{1.0F, 1.0F, 1.0F, 1.0F};
      if (primitive.material != nullptr &&
          primitive.material->has_pbr_metallic_roughness) {
        const cgltf_float *factor =
            primitive.material->pbr_metallic_roughness.base_color_factor;
        baseColor = {factor[0], factor[1], factor[2], factor[3]};
      }
      const std::uint32_t base =
          static_cast<std::uint32_t>(model.vertices.size());
      for (cgltf_size vertexIndex = 0; vertexIndex < positions->count;
           ++vertexIndex) {
        GltfSkinnedVertex3D vertex;
        float position[3]{};
        float uv[2]{};
        cgltf_uint jointValues[4]{};
        float weightValues[4]{};
        readVec(positions, vertexIndex, position, 3);
        readVec(texcoords, vertexIndex, uv, 2);
        if (joints != nullptr)
          cgltf_accessor_read_uint(joints, vertexIndex, jointValues, 4);
        readVec(weights, vertexIndex, weightValues, 4);
        vertex.position = {position[0], position[1], position[2]};
        vertex.uv = {uv[0], uv[1]};
        vertex.color = baseColor;
        for (std::size_t influence = 0; influence < 4; ++influence) {
          vertex.joints[influence] =
              static_cast<std::uint16_t>(jointValues[influence]);
          vertex.weights[influence] = weightValues[influence];
        }
        vertex.node = static_cast<int>(node);
        vertex.skin = ownerSkin;
        model.vertices.push_back(vertex);
      }
      const cgltf_size indexCount = primitive.indices != nullptr
                                        ? primitive.indices->count
                                        : positions->count;
      for (cgltf_size index = 0; index < indexCount; ++index)
        model.indices.push_back(
            base + static_cast<std::uint32_t>(
                       primitive.indices != nullptr
                           ? cgltf_accessor_read_index(primitive.indices, index)
                           : index));
    }
  }

  model.clips.reserve(profile.importAnimations ? raw->animations_count : 0U);
  for (cgltf_size animationIndex = 0;
       profile.importAnimations && animationIndex < raw->animations_count;
       ++animationIndex) {
    const cgltf_animation &source = raw->animations[animationIndex];
    GltfSkinnedModel3D::Clip clip;
    clip.name = source.name != nullptr
                    ? source.name
                    : "clip_" + std::to_string(animationIndex);
    for (cgltf_size channelIndex = 0; channelIndex < source.channels_count;
         ++channelIndex) {
      const cgltf_animation_channel &sourceChannel =
          source.channels[channelIndex];
      if (sourceChannel.sampler == nullptr ||
          sourceChannel.sampler->input == nullptr ||
          sourceChannel.sampler->output == nullptr)
        continue;
      GltfSkinnedModel3D::Channel channel;
      channel.node = nodeIndex(*raw, sourceChannel.target_node);
      channel.step =
          sourceChannel.sampler->interpolation == cgltf_interpolation_type_step;
      int components = 3;
      if (sourceChannel.target_path == cgltf_animation_path_type_rotation) {
        channel.path = GltfSkinnedModel3D::ChannelPath::Rotation;
        components = 4;
      } else if (sourceChannel.target_path == cgltf_animation_path_type_scale) {
        channel.path = GltfSkinnedModel3D::ChannelPath::Scale;
      } else if (sourceChannel.target_path !=
                 cgltf_animation_path_type_translation) {
        continue;
      }
      const cgltf_accessor *input = sourceChannel.sampler->input;
      const cgltf_accessor *output = sourceChannel.sampler->output;
      channel.times.resize(input->count);
      channel.values.resize(input->count);
      const bool cubic = sourceChannel.sampler->interpolation ==
                         cgltf_interpolation_type_cubic_spline;
      for (cgltf_size key = 0; key < input->count; ++key) {
        cgltf_accessor_read_float(input, key, &channel.times[key], 1);
        const cgltf_size outputKey = cubic ? key * 3U + 1U : key;
        cgltf_accessor_read_float(output, outputKey, channel.values[key].data(),
                                  components);
        clip.duration = std::max(clip.duration, channel.times[key]);
      }
      clip.channels.push_back(std::move(channel));
    }
    model.clips.push_back(std::move(clip));
  }
  cleanup();
  if (model.vertices.empty() || model.indices.empty()) {
    error = "glTF/GLB model contains no triangle geometry.";
    return std::nullopt;
  }
  return model;
}

} // namespace demi::assets
