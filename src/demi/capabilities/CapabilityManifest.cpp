#include "demi/capabilities/CapabilityManifest.h"

#include "demi/core/Version.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/schema/Validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace demi::capabilities {
namespace {

using Json = nlohmann::json;

std::string readFile(const std::filesystem::path &path);

std::string fieldTypeName(const runtime::ComponentFieldType type) {
  using Type = runtime::ComponentFieldType;
  switch (type) {
  case Type::Boolean:
    return "boolean";
  case Type::Integer:
    return "integer";
  case Type::Number:
    return "number";
  case Type::String:
    return "string";
  case Type::Object:
    return "object";
  case Type::Vec2:
    return "vec2";
  case Type::Vec3:
    return "vec3";
  case Type::Color:
    return "color";
  case Type::Vec2Array:
    return "vec2_array";
  case Type::Vec3Array:
    return "vec3_array";
  }
  return "unknown";
}

std::string domainName(const runtime::ComponentDomain domain) {
  using Domain = runtime::ComponentDomain;
  switch (domain) {
  case Domain::Generic:
    return "generic";
  case Domain::TwoDimensional:
    return "2d";
  case Domain::ThreeDimensional:
    return "3d";
  }
  return "unknown";
}

std::map<std::string, Json> objectsByName(const Json &array,
                                          const char *nameField) {
  std::map<std::string, Json> result;
  if (!array.is_array()) {
    return result;
  }
  for (const Json &value : array) {
    if (value.is_object() && value.contains(nameField) &&
        value[nameField].is_string()) {
      result.emplace(value[nameField].get<std::string>(), value);
    }
  }
  return result;
}

std::set<std::string> stringSet(const Json &array) {
  std::set<std::string> result;
  if (!array.is_array()) {
    return result;
  }
  for (const Json &value : array) {
    if (value.is_string()) {
      result.insert(value.get<std::string>());
    }
  }
  return result;
}

std::set<std::string>
registeredCTestNames(const std::filesystem::path &sourceRoot) {
  const std::string cmake = readFile(sourceRoot / "CMakeLists.txt");
  const std::regex namePattern(R"(\bNAME\s+([A-Za-z0-9_.+-]+))");
  std::set<std::string> names;
  for (std::sregex_iterator match(cmake.begin(), cmake.end(), namePattern),
       end;
       match != end; ++match) {
    names.insert((*match)[1].str());
  }
  return names;
}

void breaking(Diagnostics &diagnostics, std::string code, std::string message,
              const std::string &path = {}) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path,
                         .suggestion =
                             "Restore the public contract or intentionally "
                             "version and update the API baseline."});
}

void addition(Diagnostics &diagnostics, std::string code, std::string message,
              const std::string &path = {}) {
  diagnostics.push_back({.severity = Severity::Info,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path,
                         .suggestion =
                             "Update the checked API baseline after reviewing "
                             "the compatible addition."});
}

void gateError(Diagnostics &diagnostics, std::string code,
               std::string message, std::string path = {}) {
  Diagnostic diagnostic;
  diagnostic.severity = Severity::Error;
  diagnostic.code = std::move(code);
  diagnostic.message = std::move(message);
  diagnostic.path = std::move(path);
  diagnostics.push_back(std::move(diagnostic));
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool pathInside(const std::filesystem::path &child,
                const std::filesystem::path &parent) {
  const auto relative = child.lexically_relative(parent);
  return !relative.empty() && *relative.begin() != "..";
}

bool sourceFile(const std::filesystem::path &path) {
  return path.extension() == ".lua" || path.extension() == ".json";
}

void verifyNoPrivateDependencies(const std::filesystem::path &projectDirectory,
                                 Diagnostics &diagnostics) {
  constexpr std::array<std::string_view, 8> forbidden{
      "src/demi/", "src\\demi\\", "build/",      "generated/",
      "editor://", "generated://", ".codex/",    ".codex\\"};
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(projectDirectory)) {
    if (!entry.is_regular_file() || !sourceFile(entry.path())) {
      continue;
    }
    const std::string contents = readFile(entry.path());
    for (const std::string_view marker : forbidden) {
      if (contents.find(marker) == std::string::npos) {
        continue;
      }
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "CAPABILITY_GATE_PRIVATE_DEPENDENCY",
           .message = "Reference game source depends on private, build, "
                      "generated-only, or editor-only state: " +
                      std::string(marker),
           .path = entry.path().string(),
           .suggestion =
               "Use a public engine API, project-local authored source, or a "
               "validated asset://, prefab://, scene://, or script:// ID."});
    }
  }
}

} // namespace

Json buildManifest(const std::span<const std::string> luaApi) {
  std::vector<Json> components;
  for (const auto &descriptor :
       runtime::scene_loading::componentDescriptors()) {
    Json fields = Json::array();
    for (const runtime::ComponentFieldDescriptor &field : descriptor.fields) {
      fields.push_back({{"name", field.name},
                        {"type", fieldTypeName(field.type)},
                        {"required", field.required},
                        {"replicated", field.replicated}});
    }
    components.push_back(
        {{"name", descriptor.name},
         {"domain", domainName(descriptor.domain)},
         {"lua_exposed", descriptor.exposedToLua},
         {"fields", std::move(fields)}});
  }
  std::ranges::sort(components, {}, [](const Json &value) {
    return value.value("name", std::string{});
  });

  std::vector<std::string> sortedApi(luaApi.begin(), luaApi.end());
  std::ranges::sort(sortedApi);
  sortedApi.erase(std::unique(sortedApi.begin(), sortedApi.end()),
                  sortedApi.end());

  return {{"format_version", 1},
          {"engine_version", EngineVersion},
          {"components", std::move(components)},
          {"lua_api", std::move(sortedApi)}};
}

Diagnostics comparePublicApi(const Json &baseline, const Json &current) {
  Diagnostics diagnostics;
  if (!baseline.is_object() || baseline.value("format_version", 0) != 1) {
    breaking(diagnostics, "CAPABILITY_BASELINE_INVALID",
             "Public API baseline must be a format_version 1 object.");
    return diagnostics;
  }
  if (!current.is_object() || current.value("format_version", 0) != 1) {
    breaking(diagnostics, "CAPABILITY_MANIFEST_INVALID",
             "Current capability manifest must be a format_version 1 object.");
    return diagnostics;
  }

  const std::set<std::string> baselineApi =
      stringSet(baseline.value("lua_api", Json::array()));
  const std::set<std::string> currentApi =
      stringSet(current.value("lua_api", Json::array()));
  for (const std::string &api : baselineApi) {
    if (!currentApi.contains(api)) {
      breaking(diagnostics, "CAPABILITY_LUA_API_REMOVED",
               "Public Lua API was removed: " + api, api);
    }
  }
  for (const std::string &api : currentApi) {
    if (!baselineApi.contains(api)) {
      addition(diagnostics, "CAPABILITY_LUA_API_ADDED",
               "Public Lua API was added: " + api, api);
    }
  }

  const auto baselineComponents =
      objectsByName(baseline.value("components", Json::array()), "name");
  const auto currentComponents =
      objectsByName(current.value("components", Json::array()), "name");
  for (const auto &[name, oldComponent] : baselineComponents) {
    const auto currentComponent = currentComponents.find(name);
    if (currentComponent == currentComponents.end()) {
      breaking(diagnostics, "CAPABILITY_COMPONENT_REMOVED",
               "Public component was removed: " + name, name);
      continue;
    }
    const Json &value = currentComponent->second;
    if (oldComponent.value("domain", "") != value.value("domain", "")) {
      breaking(diagnostics, "CAPABILITY_COMPONENT_DOMAIN_CHANGED",
               "Component domain changed: " + name, name);
    }
    if (oldComponent.value("lua_exposed", false) &&
        !value.value("lua_exposed", false)) {
      breaking(diagnostics, "CAPABILITY_COMPONENT_LUA_REMOVED",
               "Component lost Lua exposure: " + name, name);
    }

    const auto baselineFields =
        objectsByName(oldComponent.value("fields", Json::array()), "name");
    const auto currentFields =
        objectsByName(value.value("fields", Json::array()), "name");
    for (const auto &[fieldName, oldField] : baselineFields) {
      const auto currentField = currentFields.find(fieldName);
      const std::string path = name + "." + fieldName;
      if (currentField == currentFields.end()) {
        breaking(diagnostics, "CAPABILITY_COMPONENT_FIELD_REMOVED",
                 "Public component field was removed: " + path, path);
        continue;
      }
      const Json &field = currentField->second;
      if (oldField.value("type", "") != field.value("type", "")) {
        breaking(diagnostics, "CAPABILITY_COMPONENT_FIELD_TYPE_CHANGED",
                 "Public component field type changed: " + path, path);
      }
      if (!oldField.value("required", false) &&
          field.value("required", false)) {
        breaking(diagnostics, "CAPABILITY_COMPONENT_FIELD_NOW_REQUIRED",
                 "Optional component field became required: " + path, path);
      }
      if (oldField.value("replicated", false) !=
          field.value("replicated", false)) {
        breaking(diagnostics, "CAPABILITY_COMPONENT_REPLICATION_CHANGED",
                 "Component field replication contract changed: " + path,
                 path);
      }
    }
    for (const auto &[fieldName, field] : currentFields) {
      if (baselineFields.contains(fieldName)) {
        continue;
      }
      const std::string path = name + "." + fieldName;
      if (field.value("required", false)) {
        breaking(diagnostics, "CAPABILITY_REQUIRED_FIELD_ADDED",
                 "A required field was added to a public component: " + path,
                 path);
      } else {
        addition(diagnostics, "CAPABILITY_COMPONENT_FIELD_ADDED",
                 "An optional public component field was added: " + path,
                 path);
      }
    }
  }
  for (const auto &[name, _] : currentComponents) {
    if (!baselineComponents.contains(name)) {
      addition(diagnostics, "CAPABILITY_COMPONENT_ADDED",
               "Public component was added: " + name, name);
    }
  }
  return diagnostics;
}

Diagnostics verifyReferenceGates(const std::filesystem::path &sourceRoot,
                                 const Json &document) {
  Diagnostics diagnostics;
  if (!document.is_object() || document.value("format_version", 0) != 1 ||
      !document.contains("gates") || !document["gates"].is_array() ||
      !document.contains("gaps") || !document["gaps"].is_array()) {
    gateError(diagnostics, "CAPABILITY_GATES_INVALID",
              "Reference gates must contain format_version 1, gates, and "
              "gaps arrays.");
    return diagnostics;
  }

  std::set<std::string> gapIds;
  for (const Json &gap : document["gaps"]) {
    const std::string id =
        gap.is_object() ? gap.value("id", std::string{}) : std::string{};
    if (id.empty() || !gapIds.insert(id).second) {
      gateError(diagnostics, "CAPABILITY_GAP_ID_INVALID",
                "Every capability gap needs a unique non-empty ID.");
      continue;
    }
    const std::string status = gap.value("status", std::string{});
    if (!gap.contains("phase") || !gap["phase"].is_number_integer() ||
        (status != "open" && status != "resolved") ||
        gap.value("summary", std::string{}).empty()) {
      gateError(diagnostics, "CAPABILITY_GAP_INVALID",
                "Every capability gap needs an integer phase, open or "
                "resolved status, and non-empty summary.",
                id);
    }
  }

  const std::filesystem::path canonicalRoot =
      std::filesystem::weakly_canonical(sourceRoot);
  const std::filesystem::path examplesRoot = canonicalRoot / "examples";
  const std::set<std::string> testNames = registeredCTestNames(canonicalRoot);
  std::set<std::string> gateIds;
  for (const Json &gate : document["gates"]) {
    if (!gate.is_object()) {
      gateError(diagnostics, "CAPABILITY_GATE_INVALID",
                "Every reference gate must be an object.");
      continue;
    }
    const std::string id = gate.value("id", std::string{});
    const std::string project = gate.value("project", std::string{});
    if (id.empty() || !gateIds.insert(id).second) {
      gateError(diagnostics, "CAPABILITY_GATE_ID_INVALID",
                "Every reference gate needs a unique non-empty ID.");
    }
    if (project.empty()) {
      gateError(diagnostics, "CAPABILITY_GATE_PROJECT_MISSING",
                "Reference gate has no project path.", id);
      continue;
    }

    const std::filesystem::path projectPath =
        std::filesystem::weakly_canonical(canonicalRoot / project);
    if (!pathInside(projectPath, examplesRoot)) {
      gateError(diagnostics, "CAPABILITY_GATE_OUTSIDE_EXAMPLES",
                "Reference gate project must live under examples/.",
                projectPath.string());
      continue;
    }
    if (!std::filesystem::is_regular_file(projectPath)) {
      gateError(diagnostics, "CAPABILITY_GATE_PROJECT_NOT_FOUND",
                "Reference gate project was not found.",
                projectPath.string());
      continue;
    }

    const ValidationSummary validation = validatePath(projectPath);
    diagnostics.insert(diagnostics.end(), validation.diagnostics.begin(),
                       validation.diagnostics.end());
    verifyNoPrivateDependencies(projectPath.parent_path(), diagnostics);

    if (!gate.contains("platforms") || !gate["platforms"].is_array() ||
        gate["platforms"].empty()) {
      gateError(diagnostics, "CAPABILITY_GATE_PLATFORMS_MISSING",
                "Reference gate must declare at least one platform.", id);
    } else {
      for (const Json &platform : gate["platforms"]) {
        if (!platform.is_string() ||
            (platform != "linux" && platform != "android")) {
          gateError(diagnostics, "CAPABILITY_GATE_PLATFORM_UNKNOWN",
                    "Reference gate platform must be linux or android.", id);
        }
      }
    }
    if (!gate.contains("required_tests") ||
        !gate["required_tests"].is_array() ||
        gate["required_tests"].empty()) {
      gateError(diagnostics, "CAPABILITY_GATE_TESTS_MISSING",
                "Reference gate must name its required CI tests.", id);
    } else {
      for (const Json &test : gate["required_tests"]) {
        if (!test.is_string() ||
            !testNames.contains(test.get<std::string>())) {
          gateError(diagnostics, "CAPABILITY_GATE_TEST_UNKNOWN",
                    "Reference gate names a test that is not registered with "
                    "CTest.",
                    id);
        }
      }
    }
    if (!gate.contains("gap_ids") || !gate["gap_ids"].is_array()) {
      gateError(diagnostics, "CAPABILITY_GATE_GAPS_MISSING",
                "Reference gate must explicitly declare gap_ids, even when "
                "the array is empty.",
                id);
    } else {
      for (const Json &gap : gate["gap_ids"]) {
        if (!gap.is_string() ||
            !gapIds.contains(gap.get<std::string>())) {
          gateError(diagnostics, "CAPABILITY_GATE_GAP_UNKNOWN",
                    "Reference gate points to an unknown capability gap.", id);
        }
      }
    }
  }
  return diagnostics;
}

} // namespace demi::capabilities
