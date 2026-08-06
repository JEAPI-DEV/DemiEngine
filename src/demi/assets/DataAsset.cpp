#include "demi/assets/DataAsset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>

namespace demi::assets {
namespace {

using Json = nlohmann::json;

void error(Diagnostics &diagnostics, std::string code, std::string message,
           const std::filesystem::path &path, const std::string &pointer = {}) {
  diagnostics.push_back(
      {.severity = Severity::Error,
       .code = std::move(code),
       .message = std::move(message),
       .path = pointer.empty() ? path.string() : path.string() + "#" + pointer,
       .suggestion = {}});
}

std::string valueType(const DataValue &value) {
  if (value.isNull())
    return "null";
  if (value.isBoolean())
    return "boolean";
  if (value.isInteger())
    return "integer";
  if (value.isNumber())
    return "number";
  if (value.isString())
    return "string";
  if (value.isArray())
    return "array";
  return "object";
}

bool matchesType(const DataValue &value, const std::string_view type) {
  return type == valueType(value) || (type == "number" && value.isNumber());
}

bool equalScalar(const DataValue &value, const Json &expected) {
  if (expected.is_null())
    return value.isNull();
  if (expected.is_boolean())
    return value.isBoolean() && std::get<bool>(value.value) == expected;
  if (expected.is_number_integer())
    return value.isInteger() &&
           std::get<std::int64_t>(value.value) == expected.get<std::int64_t>();
  if (expected.is_number()) {
    const double actual =
        value.isInteger()
            ? static_cast<double>(std::get<std::int64_t>(value.value))
        : value.isNumber() ? std::get<double>(value.value)
                           : 0.0;
    return value.isNumber() && actual == expected.get<double>();
  }
  return expected.is_string() && value.isString() &&
         std::get<std::string>(value.value) == expected.get<std::string>();
}

void validateValue(const DataValue &value, const Json &schema,
                   const AssetRegistry &registry,
                   const std::filesystem::path &path,
                   const std::string &pointer, Diagnostics &diagnostics,
                   std::set<std::string> &references) {
  if (!schema.is_object()) {
    error(diagnostics, "DATA_SCHEMA_NODE_INVALID",
          "Schema nodes must be JSON objects.", path, pointer);
    return;
  }
  if (const auto type = schema.find("type"); type != schema.end()) {
    if (!type->is_string() || !matchesType(value, type->get<std::string>())) {
      error(diagnostics, "DATA_SCHEMA_TYPE_MISMATCH",
            "Expected " +
                (type->is_string() ? type->get<std::string>() : "valid type") +
                " but found " + valueType(value) + ".",
            path, pointer);
      return;
    }
  }
  if (const auto values = schema.find("enum"); values != schema.end()) {
    if (!values->is_array() ||
        std::ranges::none_of(*values, [&](const Json &item) {
          return equalScalar(value, item);
        }))
      error(diagnostics, "DATA_SCHEMA_ENUM_MISMATCH",
            "Value is not one of the schema enum choices.", path, pointer);
  }
  if (value.isNumber()) {
    const double number =
        value.isInteger()
            ? static_cast<double>(std::get<std::int64_t>(value.value))
            : std::get<double>(value.value);
    if (schema.contains("minimum") && schema["minimum"].is_number() &&
        number < schema["minimum"].get<double>())
      error(diagnostics, "DATA_SCHEMA_MINIMUM", "Number is below minimum.",
            path, pointer);
    if (schema.contains("maximum") && schema["maximum"].is_number() &&
        number > schema["maximum"].get<double>())
      error(diagnostics, "DATA_SCHEMA_MAXIMUM", "Number is above maximum.",
            path, pointer);
  }
  if (value.isString() && schema.contains("reference")) {
    if (!schema["reference"].is_string()) {
      error(diagnostics, "DATA_SCHEMA_REFERENCE_INVALID",
            "Schema reference kind must be a string.", path, pointer);
    } else {
      const std::string kind = schema["reference"].get<std::string>();
      const std::string &reference = std::get<std::string>(value.value);
      const std::string prefix = kind + "://";
      if ((kind != "asset" && kind != "prefab" && kind != "scene") ||
          !reference.starts_with(prefix)) {
        error(diagnostics, "DATA_REFERENCE_INVALID",
              "Value is not a valid " + kind + " reference.", path, pointer);
      } else {
        references.insert(reference);
        if (kind == "asset" && findAsset(registry, reference) == nullptr)
          error(diagnostics, "DATA_REFERENCE_NOT_FOUND",
                "Referenced asset does not exist: " + reference, path, pointer);
      }
    }
  }
  if (const auto *object = value.object()) {
    if (const auto required = schema.find("required");
        required != schema.end()) {
      if (!required->is_array()) {
        error(diagnostics, "DATA_SCHEMA_REQUIRED_INVALID",
              "Schema required must be an array.", path, pointer);
      } else {
        for (const Json &key : *required)
          if (key.is_string() && !object->contains(key.get<std::string>()))
            error(diagnostics, "DATA_SCHEMA_REQUIRED_MISSING",
                  "Required property is missing: " + key.get<std::string>(),
                  path, pointer + "/" + key.get<std::string>());
      }
    }
    if (const auto properties = schema.find("properties");
        properties != schema.end()) {
      if (!properties->is_object()) {
        error(diagnostics, "DATA_SCHEMA_PROPERTIES_INVALID",
              "Schema properties must be an object.", path, pointer);
      } else {
        for (const auto &[key, childSchema] : properties->items()) {
          const auto child = object->find(key);
          if (child != object->end())
            validateValue(child->second, childSchema, registry, path,
                          pointer + "/" + key, diagnostics, references);
        }
      }
    }
  }
  if (const auto *array = value.array(); schema.contains("items")) {
    for (std::size_t index = 0; index < array->size(); ++index)
      validateValue((*array)[index], schema["items"], registry, path,
                    pointer + "/" + std::to_string(index), diagnostics,
                    references);
  }
}

std::optional<Json> loadSchema(const AssetManifest &manifest,
                               Diagnostics &diagnostics) {
  const DataDocumentResult parsed = loadDataDocument(manifest.sourcePath);
  diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(),
                     parsed.diagnostics.end());
  if (!parsed.document)
    return std::nullopt;
  try {
    std::ifstream input(manifest.sourcePath);
    return Json::parse(input);
  } catch (const Json::exception &exception) {
    error(diagnostics, "DATA_SCHEMA_INVALID_JSON", exception.what(),
          manifest.sourcePath);
    return std::nullopt;
  }
}

} // namespace

std::optional<DataAssetMetadata>
dataAssetMetadata(const AssetManifest &manifest, Diagnostics *diagnostics) {
  try {
    const Json settings = Json::parse(manifest.settingsJson);
    if (!settings.is_object()) {
      if (diagnostics != nullptr)
        error(*diagnostics, "DATA_ASSET_SETTINGS_INVALID",
              "DataAsset settings must be an object.", manifest.manifestPath);
      return std::nullopt;
    }
    DataAssetMetadata result;
    result.schema = settings.value("schema", "");
    result.contentType = settings.value("content_type", "");
    result.tags = settings.value("tags", std::vector<std::string>{});
    std::ranges::sort(result.tags);
    result.tags.erase(std::unique(result.tags.begin(), result.tags.end()),
                      result.tags.end());
    return result;
  } catch (const Json::exception &exception) {
    if (diagnostics != nullptr)
      error(*diagnostics, "DATA_ASSET_SETTINGS_INVALID", exception.what(),
            manifest.manifestPath);
    return std::nullopt;
  }
}

std::optional<LoadedDataAsset> loadDataAsset(const AssetManifest &manifest,
                                             const DataDocumentLimits &limits,
                                             Diagnostics *diagnostics) {
  Diagnostics local;
  const auto metadata = dataAssetMetadata(manifest, &local);
  const DataDocumentResult parsed =
      loadDataDocument(manifest.sourcePath, limits);
  local.insert(local.end(), parsed.diagnostics.begin(),
               parsed.diagnostics.end());
  const DataValue *version =
      parsed.document ? parsed.document->root().find("format_version")
                      : nullptr;
  if (parsed.document && (version == nullptr || !version->isInteger() ||
                          std::get<std::int64_t>(version->value) != 1))
    error(local, "DATA_FORMAT_VERSION_UNSUPPORTED",
          "Data documents require format_version 1.", manifest.sourcePath,
          "/format_version");
  if (diagnostics != nullptr)
    diagnostics->insert(diagnostics->end(), local.begin(), local.end());
  if (!metadata || !parsed.document || hasErrors(local))
    return std::nullopt;
  return LoadedDataAsset{.manifest = &manifest,
                         .metadata = *metadata,
                         .document = parsed.document};
}

Diagnostics validateDataAssets(const AssetRegistry &registry,
                               const DataDocumentLimits &limits) {
  Diagnostics diagnostics;
  for (const AssetManifest &manifest : registry.assets) {
    if (manifest.type != "DataAsset")
      continue;
    auto asset = loadDataAsset(manifest, limits, &diagnostics);
    if (!asset)
      continue;
    if (asset->metadata.contentType.empty())
      error(diagnostics, "DATA_CONTENT_TYPE_MISSING",
            "DataAsset settings require content_type.", manifest.manifestPath,
            "/settings/content_type");
    if (asset->metadata.schema.empty())
      continue;
    const AssetManifest *schema = findAsset(registry, asset->metadata.schema);
    if (schema == nullptr || schema->type != "DataSchema") {
      error(diagnostics, "DATA_SCHEMA_NOT_FOUND",
            "Data schema asset was not found: " + asset->metadata.schema,
            manifest.manifestPath, "/settings/schema");
      continue;
    }
    if (std::ranges::find(manifest.dependencies, asset->metadata.schema) ==
        manifest.dependencies.end())
      error(diagnostics, "DATA_DEPENDENCY_UNDECLARED",
            "Schema must be declared in manifest dependencies: " +
                asset->metadata.schema,
            manifest.manifestPath, "/dependencies");
    const auto schemaDocument = loadSchema(*schema, diagnostics);
    if (!schemaDocument)
      continue;
    if (schemaDocument->value("format_version", 0) != 1) {
      error(diagnostics, "DATA_SCHEMA_VERSION_UNSUPPORTED",
            "Data schemas require format_version 1.", schema->sourcePath,
            "/format_version");
      continue;
    }
    std::set<std::string> references;
    validateValue(asset->document->root(), *schemaDocument, registry,
                  manifest.sourcePath, "", diagnostics, references);
    for (const std::string &reference : references)
      if (reference.starts_with("asset://") &&
          std::ranges::find(manifest.dependencies, reference) ==
              manifest.dependencies.end())
        error(diagnostics, "DATA_DEPENDENCY_UNDECLARED",
              "Schema-declared reference must appear in dependencies: " +
                  reference,
              manifest.manifestPath, "/dependencies");
  }
  return diagnostics;
}

} // namespace demi::assets
