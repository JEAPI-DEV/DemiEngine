#include "cli/BuildCommands.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/app/RuntimeApp.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/schema/Validation.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int ExitSuccess = 0;
constexpr int ExitValidationFailure = 1;
constexpr int ExitUsageError = 2;

std::filesystem::path sourceRoot() {
#ifdef DEMI_SOURCE_DIR
  return std::filesystem::path(DEMI_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

void printHelp() {
  std::cout
      << "DemiEngine CLI\n"
      << "Usage:\n"
      << "  demi --help\n"
      << "  demi version\n"
      << "  demi validate [path] [--format text|json]\n"
      << "  demi schema export\n"
      << "  demi scene list <project>\n"
      << "  demi scene inspect <scene>\n"
      << "  demi scene diff <old> <new>\n"
      << "  demi asset inspect <asset>\n"
      << "  demi asset deps <asset>\n"
      << "  demi save inspect <save>\n"
      << "  demi script check <script>\n"
      << "  demi lua-stubs generate [path]\n"
      << "  demi test\n"
      << "  demi run --project <project> [--frames count|--max-frames count]\n"
      << "  demi run linux [--project <project>] [--frames count|--max-frames "
         "count]\n"
      << "  demi serve --project <project>\n"
      << "  demi build apk [--project <project>] [--gradle gradle]\n"
      << "  demi build linux [--project <project>] [--output path]\n"
      << "  demi build linux_server [--project <project>] [--output path]\n"
      << "  demi editor --project <project>\n";
}

bool hasArg(const std::vector<std::string> &args, const std::string &needle) {
  for (const std::string &arg : args) {
    if (arg == needle) {
      return true;
    }
  }
  return false;
}

std::string valueAfter(const std::vector<std::string> &args,
                       const std::string &key) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == key) {
      return args[i + 1];
    }
  }
  return {};
}

int runValidate(const std::vector<std::string> &args) {
  std::filesystem::path target = ".";
  std::string format = "text";

  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--format" && i + 1 < args.size()) {
      format = args[i + 1];
      ++i;
      continue;
    }
    if (!args[i].starts_with("--")) {
      target = args[i];
    }
  }

  const demi::ValidationSummary summary = demi::validatePath(target);
  if (format == "json") {
    demi::printDiagnosticsJson(std::cout, summary.diagnostics);
  } else {
    std::cout << "Checked " << summary.checkedFiles << " file(s).\n";
    demi::printDiagnosticsText(std::cout, summary.diagnostics);
  }

  return demi::hasErrors(summary.diagnostics) ? ExitValidationFailure
                                              : ExitSuccess;
}

int numericValueAfter(const std::vector<std::string> &args,
                      const std::string &key) {
  const std::string value = valueAfter(args, key);
  if (value.empty()) {
    return 0;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return 0;
  }
}

int frameLimitFrom(const std::vector<std::string> &args) {
  const int maxFrames = numericValueAfter(args, "--max-frames");
  return maxFrames > 0 ? maxFrames : numericValueAfter(args, "--frames");
}

int runProjectCommand(const std::vector<std::string> &args,
                      const bool serve = false) {
  const std::filesystem::path project = demi::cli::projectFileFromArgs(args);
  if (project.empty()) {
    std::cerr << "run requires --project <project> or a demi.project.json in "
                 "the current directory.\n";
    return ExitUsageError;
  }
  return demi::runtime::runProject(demi::runtime::RuntimeOptions{
      .projectPath = project,
      .maxFrames = frameLimitFrom(args),
      .serve = serve,
  });
}

int runScene(const std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::cerr << "scene requires a subcommand.\n";
    return ExitUsageError;
  }

  const std::string &subcommand = args[1];
  if (subcommand == "list") {
    const std::filesystem::path project = args[2];
    const auto references = demi::extractSceneReferences(project);
    for (const std::string &reference : references) {
      std::cout << reference << '\n';
    }
    return references.empty() ? ExitValidationFailure : ExitSuccess;
  }

  if (subcommand == "inspect") {
    const std::filesystem::path scene = args[2];
    const demi::ValidationSummary summary = demi::validatePath(scene);
    demi::printDiagnosticsText(std::cout, summary.diagnostics);
    if (!demi::hasErrors(summary.diagnostics)) {
      std::cout << "Scene inspection complete: " << scene.string() << '\n';
    }
    return demi::hasErrors(summary.diagnostics) ? ExitValidationFailure
                                                : ExitSuccess;
  }

  if (subcommand == "diff") {
    if (args.size() < 4) {
      std::cerr << "scene diff requires <old> and <new>.\n";
      return ExitUsageError;
    }
    std::cout << "Scene diff placeholder: " << args[2] << " -> " << args[3]
              << '\n';
    return ExitSuccess;
  }

  std::cerr << "Unknown scene subcommand: " << subcommand << '\n';
  return ExitUsageError;
}

int runSave(const std::vector<std::string> &args) {
  if (args.size() < 3 || args[1] != "inspect") {
    std::cerr << "save requires: demi save inspect <save>.\n";
    return ExitUsageError;
  }

  const std::filesystem::path save = args[2];
  const demi::ValidationSummary summary = demi::validatePath(save);
  demi::printDiagnosticsText(std::cout, summary.diagnostics);
  if (!demi::hasErrors(summary.diagnostics)) {
    std::cout << "Save inspection complete: " << save.string() << '\n';
  }
  return demi::hasErrors(summary.diagnostics) ? ExitValidationFailure
                                              : ExitSuccess;
}

int runAsset(const std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::cerr << "asset requires a subcommand and asset manifest path.\n";
    return ExitUsageError;
  }

  const std::filesystem::path manifestPath = args[2];
  demi::Diagnostic diagnostic;
  const std::optional<demi::AssetManifest> manifest =
      demi::loadAssetManifest(manifestPath, &diagnostic);
  if (!manifest.has_value()) {
    demi::printDiagnosticsText(std::cerr, demi::Diagnostics{diagnostic});
    return ExitValidationFailure;
  }

  if (args[1] == "inspect") {
    std::cout << "id: " << manifest->id << '\n';
    std::cout << "type: " << manifest->type << '\n';
    std::cout << "source: " << manifest->sourcePath.string() << '\n';
    std::cout << "source_exists: "
              << (std::filesystem::exists(manifest->sourcePath) ? "true"
                                                                : "false")
              << '\n';
    return std::filesystem::exists(manifest->sourcePath)
               ? ExitSuccess
               : ExitValidationFailure;
  }

  if (args[1] == "deps") {
    std::cout << manifest->sourcePath.string() << '\n';
    return ExitSuccess;
  }

  std::cerr << "Unknown asset subcommand: " << args[1] << '\n';
  return ExitUsageError;
}

int runScript(const std::vector<std::string> &args) {
  if (args.size() < 3 || args[1] != "check") {
    std::cerr << "script requires: demi script check <script>.\n";
    return ExitUsageError;
  }

  const demi::Diagnostics diagnostics =
      demi::runtime::LuaScriptHost::checkScriptSyntax(args[2]);
  demi::printDiagnosticsText(std::cout, diagnostics);
  return demi::hasErrors(diagnostics) ? ExitValidationFailure : ExitSuccess;
}

int runLuaStubs(const std::vector<std::string> &args) {
  if (args.size() < 2 || args[1] != "generate") {
    std::cerr << "lua-stubs requires: demi lua-stubs generate [path].\n";
    return ExitUsageError;
  }

  const std::filesystem::path outputPath =
      args.size() >= 3 ? std::filesystem::path(args[2])
                       : std::filesystem::path("scripts/stubs/demi.lua");
  std::error_code error;
  if (!outputPath.parent_path().empty()) {
    std::filesystem::create_directories(outputPath.parent_path(), error);
  }
  if (error) {
    std::cerr << "Failed to create stub directory: "
              << outputPath.parent_path().string() << '\n';
    return ExitValidationFailure;
  }

  const std::filesystem::path sourcePath =
      sourceRoot() / "scripts" / "stubs" / "demi.lua";
  std::ifstream input(sourcePath);
  if (!input) {
    std::cerr << "Failed to read Lua stubs: " << sourcePath.string() << '\n';
    return ExitValidationFailure;
  }

  std::ofstream output(outputPath);
  if (!output) {
    std::cerr << "Failed to write Lua stubs: " << outputPath.string() << '\n';
    return ExitValidationFailure;
  }

  output << input.rdbuf();
  std::cout << "Wrote LuaLS stubs: " << outputPath.string() << '\n';
  return ExitSuccess;
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (args.empty() || args[0] == "--help" || args[0] == "help") {
    printHelp();
    return ExitSuccess;
  }

  if (args[0] == "version") {
    std::cout << demi::EngineName << ' ' << demi::EngineVersion << '\n';
    return ExitSuccess;
  }

  if (args[0] == "validate") {
    return runValidate(args);
  }

  if (args[0] == "schema" && args.size() >= 2 && args[1] == "export") {
    const std::filesystem::path outputPath =
        args.size() >= 3 ? std::filesystem::path(args[2])
                         : sourceRoot() / "schemas" / "components.schema.json";
    std::ofstream output(outputPath);
    if (!output) {
      std::cerr << "Failed to write component schema: " << outputPath << '\n';
      return ExitValidationFailure;
    }
    output << demi::runtime::scene_loading::canonicalComponentSchema().dump(2)
           << '\n';
    std::cout << "Wrote component schema: " << outputPath << '\n';
    return ExitSuccess;
  }

  if (args[0] == "scene") {
    return runScene(args);
  }

  if (args[0] == "asset") {
    return runAsset(args);
  }

  if (args[0] == "save") {
    return runSave(args);
  }

  if (args[0] == "script") {
    return runScript(args);
  }

  if (args[0] == "lua-stubs") {
    return runLuaStubs(args);
  }

  if (args[0] == "test") {
    const demi::ValidationSummary summary = demi::validatePath("examples");
    demi::printDiagnosticsText(std::cout, summary.diagnostics);
    return demi::hasErrors(summary.diagnostics) ? ExitValidationFailure
                                                : ExitSuccess;
  }

  if (args[0] == "build") {
    return demi::cli::runBuildCommand(args, demi::cli::BuildContext{
                                                .engineRoot = sourceRoot(),
                                                .executablePath = argv[0],
                                            });
  }

  if (args[0] == "run") {
    if (args.size() >= 2 && !args[1].starts_with("--") && args[1] != "linux") {
      std::cerr << "Unknown run target: " << args[1] << '\n';
      return ExitUsageError;
    }
    return runProjectCommand(args);
  }

  if (args[0] == "serve") {
    return runProjectCommand(args, true);
  }

  if (args[0] == "editor") {
    const std::string project = valueAfter(args, "--project");
    if (project.empty()) {
      std::cerr << "editor requires --project <project>.\n";
      return ExitUsageError;
    }
    std::cout << "Editor launch placeholder for project: " << project << '\n';
    return ExitSuccess;
  }

  if (hasArg(args, "--help")) {
    printHelp();
    return ExitSuccess;
  }

  std::cerr << "Unknown command: " << args[0] << '\n';
  return ExitUsageError;
}
