#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct MeshRendererComponent {
  static constexpr std::string_view typeName = "MeshRenderer";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("model").withHelp("Imported Model3D asset. Choose a model here, or leave empty to use Shape."),
      ComponentFieldDescriptor::assetReference("medium_lod_model").asAdvanced().withHelp("Optional reduced-detail model for medium camera distances."),
      ComponentFieldDescriptor{"medium_lod_distance",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.0,
                               true}.asAdvanced().withHelp("Camera distance in world units for the medium-detail model."),
      ComponentFieldDescriptor::assetReference("low_lod_model").asAdvanced().withHelp("Optional reduced-detail model for distant objects."),
      ComponentFieldDescriptor{"low_lod_distance",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.0,
                               true}.asAdvanced().withHelp("Camera distance in world units for the low-detail model."),
      ComponentFieldDescriptor{"cull_distance",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.0,
                               true}.asAdvanced().withHelp("Stop drawing beyond this camera distance. Zero disables distance culling."),
      ComponentFieldDescriptor{"shape", ComponentFieldType::String}.withHelp("Built-in geometry: cube, sphere, cylinder, or plane. Used when no model or inline vertices are supplied."),
      ComponentFieldDescriptor{"size", ComponentFieldType::Vec3}.withHelp("Mesh dimensions applied before the entity Transform scale. Built-in primitives have unit dimensions."),
      ComponentFieldDescriptor{"color", ComponentFieldType::Color}.withHelp("RGBA tint multiplied with the base-color texture. Use white to preserve texture colors."),
      ComponentFieldDescriptor::assetReference("texture").withHelp("Base-color texture override. UV coordinates determine how it is mapped onto the mesh."),
      ComponentFieldDescriptor::assetReference("material").withHelp("Material asset containing shader and rendering settings."),
      ComponentFieldDescriptor{"material_properties",
                               ComponentFieldType::Object},
      ComponentFieldDescriptor{"render_layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"vertices", ComponentFieldType::Vec3Array}.asAdvanced().withHelp("Advanced inline triangle geometry: three XYZ vertices per triangle. Usually supplied by a model or the Shape primitive."),
      ComponentFieldDescriptor{"normals", ComponentFieldType::Vec3Array}.asAdvanced().withHelp("Lighting directions for inline vertices. Omit to calculate them from the triangles; otherwise provide one XYZ normal per vertex."),
      ComponentFieldDescriptor{"uvs", ComponentFieldType::Vec2Array}.asAdvanced().withHelp("Texture coordinates for inline vertices, one UV pair per vertex. 0..1 spans the image; larger values tile when texture Wrap is Repeat."),
      ComponentFieldDescriptor{"wireframe", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"3D", "Mesh Renderer",
      "Choose a model or built-in shape, then assign its texture/material. Hover field labels for help."};
  static void parse(const nlohmann::json &json, Entity &entity);
  static bool serializeField(const MeshRendererComponent &component,
                             std::string_view field, nlohmann::json &out);

  std::string model;
  std::string mediumLodModel;
  float mediumLodDistance = 0.0F;
  std::string lowLodModel;
  float lowLodDistance = 0.0F;
  float cullDistance = 0.0F;
  std::string shape = "cube";
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Color color = {0.8F, 0.8F, 0.8F, 1.0F};
  std::string texture;
  std::string material;
  std::unordered_map<std::string, float> materialNumbers;
  std::unordered_map<std::string, Color> materialColors;
  std::string renderLayer;
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
  std::uint64_t revision = 0;
  Vec3 boundsMin;
  Vec3 boundsMax;
  bool hasBounds = false;
  bool wireframe = false;
  void markGeometryChanged();
  static void afterRuntimeFieldChange(MeshRendererComponent &mesh,
                                      std::string_view field);
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::model>("model"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::mediumLodModel>("medium_lod_model"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::mediumLodDistance>("medium_lod_distance"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::lowLodModel>("low_lod_model"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::lowLodDistance>("low_lod_distance"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::cullDistance>("cull_distance"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::shape>("shape"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::size>("size"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::color>("color"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::texture>("texture"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::material>("material"),
      RuntimeFieldBinding<MeshRendererComponent>::members<
          &MeshRendererComponent::materialNumbers,
          &MeshRendererComponent::materialColors>("material_properties"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::renderLayer>("render_layer"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::vertices>("vertices"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::normals>("normals"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::uvs>("uvs"),
      RuntimeFieldBinding<MeshRendererComponent>::member<
          &MeshRendererComponent::wireframe>("wireframe")};
};

} // namespace demi::runtime
