#include "demi/packages/PackageManifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace demi::packages {
namespace {

void error(Diagnostics &diagnostics, const std::filesystem::path &path,
           std::string code, std::string message, std::string suggestion = {}) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = std::move(suggestion)});
}

bool readStringArray(const nlohmann::json &document, const char *field,
                     std::vector<std::string> &target, Diagnostics &diagnostics,
                     const std::filesystem::path &path) {
  const auto value = document.find(field);
  if (value == document.end())
    return true;
  if (!value->is_array()) {
    error(diagnostics, path, "PACKAGE_MANIFEST_FIELD_TYPE",
          std::string(field) + " must be an array of strings.");
    return false;
  }
  std::set<std::string> unique;
  for (const auto &entry : *value) {
    if (!entry.is_string() || entry.get<std::string>().empty()) {
      error(diagnostics, path, "PACKAGE_MANIFEST_FIELD_TYPE",
            std::string(field) + " contains a non-string or empty entry.");
      continue;
    }
    const std::string text = entry.get<std::string>();
    if (!unique.insert(text).second) {
      error(diagnostics, path, "PACKAGE_MANIFEST_DUPLICATE_ENTRY",
            std::string(field) + " contains a duplicate: " + text);
      continue;
    }
    target.push_back(text);
  }
  return !hasErrors(diagnostics);
}

} // namespace

bool validPackageName(const std::string_view name) {
  if (name.empty() || name.size() > 128 || name.front() == '.' ||
      name.back() == '.' || name.find("..") != std::string_view::npos)
    return false;
  return std::ranges::all_of(name, [](const unsigned char character) {
    return std::islower(character) != 0 || std::isdigit(character) != 0 ||
           character == '.' || character == '-' || character == '_';
  });
}

bool safePackageRelativePath(const std::string_view value) {
  if (value.empty() || value.find('\\') != std::string_view::npos ||
      value.find('\0') != std::string_view::npos)
    return false;
  const std::filesystem::path path(value);
  if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
    return false;
  for (const auto &part : path)
    if (part == "." || part == ".." || part.empty())
      return false;
  return path.lexically_normal().generic_string() == value;
}

ManifestLoadResult loadPackageManifest(const std::filesystem::path &path) {
  ManifestLoadResult result;
  std::ifstream input(path);
  if (!input) {
    error(result.diagnostics, path, "PACKAGE_MANIFEST_READ_FAILED",
          "Could not read the package manifest.");
    return result;
  }
  nlohmann::json document;
  try {
    input >> document;
  } catch (const nlohmann::json::exception &exception) {
    error(result.diagnostics, path, "PACKAGE_MANIFEST_INVALID_JSON",
          exception.what());
    return result;
  }
  return parsePackageManifest(document, path.string());
}

ManifestLoadResult parsePackageManifest(const nlohmann::json &document,
                                        std::string source) {
  ManifestLoadResult result;
  const std::filesystem::path path(std::move(source));
  if (!document.is_object() || document.value("format_version", 0) != 1) {
    error(result.diagnostics, path, "PACKAGE_MANIFEST_FORMAT_UNSUPPORTED",
          "Package manifest requires format_version 1.");
    return result;
  }

  PackageManifest manifest;
  manifest.sourcePath = path;
  manifest.name = document.value("name", "");
  if (!validPackageName(manifest.name))
    error(result.diagnostics, path, "PACKAGE_NAME_INVALID",
          "Package name must use lowercase letters, digits, '.', '-', or '_'.");
  const std::string versionText = document.value("version", "");
  const auto version = SemanticVersion::parse(versionText);
  if (!version)
    error(result.diagnostics, path, "PACKAGE_VERSION_INVALID",
          "Package version is not a semantic version: " + versionText);
  else
    manifest.version = *version;
  const std::string engineText = document.value("engine", "*");
  const auto engine = VersionConstraint::parse(engineText);
  if (!engine)
    error(result.diagnostics, path, "PACKAGE_ENGINE_CONSTRAINT_INVALID",
          "Package engine constraint is invalid: " + engineText);
  else
    manifest.engineVersion = *engine;

  const auto dependencies = document.find("dependencies");
  if (dependencies != document.end()) {
    if (!dependencies->is_object()) {
      error(result.diagnostics, path, "PACKAGE_DEPENDENCIES_INVALID",
            "Package dependencies must be an object of name-to-constraint "
            "entries.");
    } else {
      for (const auto &[name, constraintValue] : dependencies->items()) {
        if (!validPackageName(name) || !constraintValue.is_string()) {
          error(result.diagnostics, path, "PACKAGE_DEPENDENCY_INVALID",
                "Package dependency is invalid: " + name);
          continue;
        }
        const auto constraint =
            VersionConstraint::parse(constraintValue.get<std::string>());
        if (!constraint) {
          error(result.diagnostics, path,
                "PACKAGE_DEPENDENCY_CONSTRAINT_INVALID",
                "Dependency constraint is invalid for " + name + ".");
          continue;
        }
        manifest.dependencies.push_back(
            {.name = name, .constraint = *constraint});
      }
    }
  }
  readStringArray(document, "public_modules", manifest.publicModules,
                  result.diagnostics, path);
  readStringArray(document, "files", manifest.files, result.diagnostics, path);
  readStringArray(document, "exported_events", manifest.exportedEvents,
                  result.diagnostics, path);

  for (const std::string &file : manifest.files)
    if (!safePackageRelativePath(file))
      error(result.diagnostics, path, "PACKAGE_FILE_PATH_UNSAFE",
            "Package declares an unsafe file path: " + file);
  for (const std::string &module : manifest.publicModules) {
    if (!validPackageName(module)) {
      error(result.diagnostics, path, "PACKAGE_MODULE_NAME_INVALID",
            "Public Lua module name is invalid: " + module);
      continue;
    }
    std::string modulePath = "scripts/";
    for (const char character : module)
      modulePath += character == '.' ? '/' : character;
    modulePath += ".lua";
    const std::string initPath =
        modulePath.substr(0, modulePath.size() - 4) + "/init.lua";
    if (std::ranges::find(manifest.files, modulePath) == manifest.files.end() &&
        std::ranges::find(manifest.files, initPath) == manifest.files.end())
      error(result.diagnostics, path, "PACKAGE_PUBLIC_MODULE_FILE_MISSING",
            "Public module has no declared Lua file: " + module);
  }
  if (hasErrors(result.diagnostics))
    return result;
  result.manifest = std::move(manifest);
  return result;
}

nlohmann::json packageManifestJson(const PackageManifest &manifest) {
  nlohmann::json dependencies = nlohmann::json::object();
  for (const auto &dependency : manifest.dependencies)
    dependencies[dependency.name] = dependency.constraint.text();
  return {{"format_version", manifest.formatVersion},
          {"name", manifest.name},
          {"version", manifest.version.string()},
          {"engine", manifest.engineVersion.text()},
          {"dependencies", std::move(dependencies)},
          {"public_modules", manifest.publicModules},
          {"files", manifest.files},
          {"exported_events", manifest.exportedEvents}};
}

} // namespace demi::packages
