#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::assets {

struct ImporterDescriptor {
  std::string name;
  // Effective selected type retained for source compatibility with the Step 2
  // importerFor API. Registry descriptors use assetTypes for capabilities.
  std::string assetType;
  int version = 1;
  int settingsSchemaVersion = 1;
  std::vector<std::string> extensions;
  std::vector<std::string> assetTypes;
  std::vector<std::string> outputTypes;
  std::vector<std::string> platforms;
  std::string settingsSchema = R"({"type":"object"})";
  bool threadSafe = true;
  bool copyToGeneratedOnImport = true;
};

struct ImportExecutionRequest {
  std::filesystem::path source;
  std::filesystem::path outputDirectory;
  std::string assetId;
  std::string assetType;
  std::string platform;
  std::string settingsJson = "{}";
};

struct ImportExecutionResult {
  std::vector<std::filesystem::path> generatedOutputs;
  std::vector<std::string> dependencies;
  std::string metadataJson = "{}";
  Diagnostics diagnostics;
};

class AssetImporter {
public:
  virtual ~AssetImporter() = default;
  [[nodiscard]] virtual ImportExecutionResult
  import(const ImportExecutionRequest &request) = 0;
};

class AssetImporterRegistry {
public:
  using Factory = std::function<std::unique_ptr<AssetImporter>()>;

  [[nodiscard]] bool registerImporter(ImporterDescriptor descriptor,
                                      Factory factory,
                                      Diagnostics *diagnostics = nullptr);
  [[nodiscard]] std::optional<ImporterDescriptor>
  select(const std::filesystem::path &source, std::string_view assetType = {},
         std::string_view explicitImporter = {},
         Diagnostics *diagnostics = nullptr) const;
  [[nodiscard]] std::unique_ptr<AssetImporter>
  create(std::string_view name) const;
  [[nodiscard]] std::vector<ImporterDescriptor> descriptors() const;

private:
  struct Registration {
    ImporterDescriptor descriptor;
    Factory factory;
  };
  std::vector<Registration> registrations_;
};

[[nodiscard]] AssetImporterRegistry createBuiltinImporterRegistry();
[[nodiscard]] const AssetImporterRegistry &builtinImporterRegistry();

} // namespace demi::assets
