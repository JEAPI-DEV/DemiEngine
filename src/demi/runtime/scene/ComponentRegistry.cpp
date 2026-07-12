#include "demi/runtime/scene/ComponentRegistry.h"

#include <algorithm>
#include <array>

namespace demi::runtime::scene_loading {

namespace {

constexpr std::array Descriptors{
#include "demi/generated/ComponentDescriptors.inc"
};

} // namespace

std::span<const ComponentDescriptor> componentDescriptors() {
  return Descriptors;
}

const ComponentDescriptor *
findComponentDescriptor(const std::string_view name) {
  for (const ComponentDescriptor &descriptor : Descriptors) {
    if (descriptor.name == name) {
      return &descriptor;
    }
  }
  return nullptr;
}

std::shared_ptr<const Component>
makeAuthoredComponent(const ComponentDescriptor &descriptor, std::string json) {
  switch (descriptor.domain) {
  case ComponentDomain::TwoDimensional:
    return std::make_shared<Component2D>(std::string(descriptor.name),
                                         std::move(json));
  case ComponentDomain::ThreeDimensional:
    return std::make_shared<Component3D>(std::string(descriptor.name),
                                         std::move(json));
  case ComponentDomain::Generic:
    return std::make_shared<GenericComponent>(std::string(descriptor.name),
                                              std::move(json));
  }
  return std::make_shared<GenericComponent>(std::string(descriptor.name),
                                            std::move(json));
}

namespace {

bool matchesFieldType(const nlohmann::json &value,
                      const ComponentFieldType type) {
  switch (type) {
  case ComponentFieldType::Boolean:
    return value.is_boolean();
  case ComponentFieldType::Integer:
    return value.is_number_integer();
  case ComponentFieldType::Number:
    return value.is_number();
  case ComponentFieldType::String:
    return value.is_string();
  case ComponentFieldType::Object:
    return value.is_object();
  case ComponentFieldType::Vec2:
    return value.is_array() && value.size() == 2 &&
           std::ranges::all_of(
               value, [](const auto &item) { return item.is_number(); });
  case ComponentFieldType::Vec3:
    return value.is_array() && value.size() == 3 &&
           std::ranges::all_of(
               value, [](const auto &item) { return item.is_number(); });
  case ComponentFieldType::Color:
    return value.is_array() && value.size() == 4 &&
           std::ranges::all_of(
               value, [](const auto &item) { return item.is_number(); });
  case ComponentFieldType::Vec2Array:
  case ComponentFieldType::Vec3Array: {
    const std::size_t width = type == ComponentFieldType::Vec2Array ? 2U : 3U;
    return value.is_array() &&
           std::ranges::all_of(value, [width](const auto &item) {
             return item.is_array() && item.size() == width &&
                    std::ranges::all_of(item, [](const auto &number) {
                      return number.is_number();
                    });
           });
  }
  }
  return false;
}

} // namespace

std::vector<ComponentValidationError>
validateComponent(const ComponentDescriptor &descriptor,
                  const nlohmann::json &json) {
  std::vector<ComponentValidationError> errors;
  if (!json.is_object()) {
    errors.push_back({.field = {}, .message = "must be an object"});
    return errors;
  }
  if (descriptor.fields.empty()) {
    return errors;
  }
  for (auto iterator = json.begin(); iterator != json.end(); ++iterator) {
    const auto field = std::ranges::find(descriptor.fields, iterator.key(),
                                         &ComponentFieldDescriptor::name);
    if (field == descriptor.fields.end()) {
      errors.push_back(
          {.field = iterator.key(), .message = "is not a recognized field"});
      continue;
    }
    if (!matchesFieldType(iterator.value(), field->type)) {
      errors.push_back(
          {.field = iterator.key(), .message = "has the wrong JSON type"});
      continue;
    }
    if (field->hasMinimum && iterator.value().is_number() &&
        iterator.value().get<double>() < field->minimum) {
      errors.push_back(
          {.field = iterator.key(), .message = "is below the allowed minimum"});
    }
    if (!field->allowedValues.empty() && iterator.value().is_string() &&
        std::ranges::find(field->allowedValues,
                          iterator.value().get<std::string>()) ==
            field->allowedValues.end()) {
      errors.push_back(
          {.field = iterator.key(), .message = "has an unsupported value"});
    }
  }
  for (const ComponentFieldDescriptor &field : descriptor.fields) {
    if (field.required && !json.contains(field.name)) {
      errors.push_back(
          {.field = std::string(field.name), .message = "is required"});
    }
  }
  return errors;
}

nlohmann::json componentDefaults(const ComponentDescriptor &descriptor) {
  return descriptor.defaults();
}

nlohmann::json componentSchema(const ComponentDescriptor &descriptor) {
  nlohmann::json schema = {{"type", "object"},
                           {"additionalProperties", false},
                           {"properties", nlohmann::json::object()}};
  for (const ComponentFieldDescriptor &field : descriptor.fields) {
    nlohmann::json property;
    switch (field.type) {
    case ComponentFieldType::Boolean:
      property["type"] = "boolean";
      break;
    case ComponentFieldType::Integer:
      property["type"] = "integer";
      break;
    case ComponentFieldType::Number:
      property["type"] = "number";
      break;
    case ComponentFieldType::String:
      property["type"] = "string";
      break;
    case ComponentFieldType::Object:
      property["type"] = "object";
      break;
    case ComponentFieldType::Vec2:
    case ComponentFieldType::Vec3:
    case ComponentFieldType::Color: {
      const int count = field.type == ComponentFieldType::Vec2   ? 2
                        : field.type == ComponentFieldType::Vec3 ? 3
                                                                 : 4;
      property = {{"type", "array"},
                  {"items", {{"type", "number"}}},
                  {"minItems", count},
                  {"maxItems", count}};
      break;
    }
    case ComponentFieldType::Vec2Array:
    case ComponentFieldType::Vec3Array: {
      const int count = field.type == ComponentFieldType::Vec2Array ? 2 : 3;
      property = {{"type", "array"},
                  {"items",
                   {{"type", "array"},
                    {"items", {{"type", "number"}}},
                    {"minItems", count},
                    {"maxItems", count}}}};
      break;
    }
    }
    if (field.hasMinimum)
      property["minimum"] = field.minimum;
    if (!field.allowedValues.empty())
      property["enum"] = field.allowedValues;
    schema["properties"][field.name] = std::move(property);
    if (field.required) {
      schema["required"].push_back(field.name);
    }
  }
  return schema;
}

nlohmann::json canonicalComponentSchema() {
  nlohmann::json properties = nlohmann::json::object();
  for (const ComponentDescriptor &descriptor : componentDescriptors()) {
    properties[descriptor.name] = componentSchema(descriptor);
  }
  return {{"type", "object"},
          {"additionalProperties", false},
          {"properties", std::move(properties)}};
}

} // namespace demi::runtime::scene_loading
