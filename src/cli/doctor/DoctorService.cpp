#include "cli/doctor/DoctorService.h"

#include "demi/schema/Validation.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <ranges>
#include <sstream>

namespace demi::cli::doctor {
namespace {

void add(Diagnostics &diagnostics, Severity severity, std::string code,
         std::string message, const std::filesystem::path &path = {},
         std::string suggestion = {}) {
  diagnostics.push_back({.severity = severity,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = std::move(suggestion)});
}

std::string valueAfter(const std::vector<std::string> &args,
                       const std::string &key) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i)
    if (args[i] == key)
      return args[i + 1];
  return {};
}

std::filesystem::path projectFileFrom(const std::vector<std::string> &args) {
  const std::string value = valueAfter(args, "--project");
  if (!value.empty()) {
    const std::filesystem::path path(value);
    return std::filesystem::is_directory(path) ? path / "demi.project.json"
                                               : path;
  }
  const auto candidate = std::filesystem::current_path() / "demi.project.json";
  return std::filesystem::is_regular_file(candidate) ? candidate
                                                     : std::filesystem::path{};
}

std::vector<std::filesystem::path> pathEntries(const std::string &path) {
  std::vector<std::filesystem::path> result;
  std::stringstream input(path);
  std::string item;
  while (std::getline(input, item, ':'))
    if (!item.empty())
      result.emplace_back(item);
  return result;
}

bool writableLocation(const DoctorEnvironment &environment,
                      std::filesystem::path path) {
  std::error_code error;
  while (!path.empty() && !std::filesystem::exists(path, error)) {
    const auto parent = path.parent_path();
    if (parent == path)
      break;
    path = parent;
  }
  return !path.empty() && environment.directoryWritable(path);
}

} // namespace

DoctorEnvironment systemDoctorEnvironment() {
  return {
      .commandAvailable =
          [](const std::string &command) {
            const char *path = std::getenv("PATH");
            if (path == nullptr)
              return false;
            return std::ranges::any_of(
                pathEntries(path), [&](const auto &entry) {
                  std::error_code error;
                  const auto candidate = entry / command;
                  const auto permissions =
                      std::filesystem::status(candidate, error).permissions();
                  return !error &&
                         std::filesystem::is_regular_file(candidate) &&
                         (permissions &
                          (std::filesystem::perms::owner_exec |
                           std::filesystem::perms::group_exec |
                           std::filesystem::perms::others_exec)) !=
                             std::filesystem::perms::none;
                });
          },
      .variable = [](const std::string &name) -> std::optional<std::string> {
        const char *value = std::getenv(name.c_str());
        if (value == nullptr || *value == '\0')
          return std::nullopt;
        return std::string(value);
      },
      .directoryWritable =
          [](const std::filesystem::path &directory) {
            std::error_code error;
            const auto permissions =
                std::filesystem::status(directory, error).permissions();
            return !error &&
                   (permissions & (std::filesystem::perms::owner_write |
                                   std::filesystem::perms::group_write |
                                   std::filesystem::perms::others_write)) !=
                       std::filesystem::perms::none;
          }};
}

DoctorService::DoctorService(DoctorEnvironment environment)
    : environment_(std::move(environment)) {}

Diagnostics DoctorService::inspect(const DoctorRequest &request) const {
  Diagnostics diagnostics;
  if (request.platform != "linux" && request.platform != "android") {
    add(diagnostics, Severity::Error, "DOCTOR_PLATFORM_UNKNOWN",
        "Unsupported doctor platform: " + request.platform, {},
        "Use linux or android.");
    return diagnostics;
  }
  if (request.projectPath.empty() ||
      !std::filesystem::is_regular_file(request.projectPath)) {
    add(diagnostics, Severity::Error, "DOCTOR_PROJECT_NOT_FOUND",
        "Project file was not found.", request.projectPath,
        "Pass --project path/to/demi.project.json.");
    return diagnostics;
  }

  const ValidationSummary validation =
      validatePath(request.projectPath.parent_path());
  diagnostics.insert(diagnostics.end(), validation.diagnostics.begin(),
                     validation.diagnostics.end());
  if (!hasErrors(validation.diagnostics))
    add(diagnostics, Severity::Info, "DOCTOR_PROJECT_VALID",
        "Project source data is valid.", request.projectPath);

  const auto projectDirectory = request.projectPath.parent_path();
  if (!environment_.directoryWritable(projectDirectory))
    add(diagnostics, Severity::Error, "DOCTOR_PROJECT_NOT_WRITABLE",
        "Project directory is not writable.", projectDirectory);

  const auto home = environment_.variable("HOME").value_or("");
  const auto dataRoot =
      environment_.variable("XDG_DATA_HOME")
          .value_or(
              home.empty()
                  ? std::string{}
                  : (std::filesystem::path(home) / ".local/share").string());
  const auto cacheRoot =
      environment_.variable("XDG_CACHE_HOME")
          .value_or(home.empty()
                        ? std::string{}
                        : (std::filesystem::path(home) / ".cache").string());
  if (dataRoot.empty())
    add(diagnostics, Severity::Warning, "DOCTOR_USER_DATA_UNRESOLVED",
        "User data location could not be resolved from HOME or XDG_DATA_HOME.");
  else if (!writableLocation(environment_, dataRoot))
    add(diagnostics, Severity::Error, "DOCTOR_USER_DATA_NOT_WRITABLE",
        "User data location is not writable.", dataRoot);
  else
    add(diagnostics, Severity::Info, "DOCTOR_USER_DATA_WRITABLE",
        "User data location is writable.", dataRoot);
  if (cacheRoot.empty())
    add(diagnostics, Severity::Warning, "DOCTOR_CACHE_UNRESOLVED",
        "Cache location could not be resolved from HOME or XDG_CACHE_HOME.");
  else if (!writableLocation(environment_, cacheRoot))
    add(diagnostics, Severity::Error, "DOCTOR_CACHE_NOT_WRITABLE",
        "Cache location is not writable.", cacheRoot);
  else
    add(diagnostics, Severity::Info, "DOCTOR_CACHE_WRITABLE",
        "Cache location is writable.", cacheRoot);

  for (const std::string command : {"cmake", "ninja", "c++"}) {
    if (environment_.commandAvailable(command))
      add(diagnostics, Severity::Info, "DOCTOR_TOOL_FOUND",
          "Found required tool: " + command);
    else
      add(diagnostics, Severity::Error, "DOCTOR_TOOL_MISSING",
          "Required tool is not available on PATH: " + command, {},
          "Install " + command + " or add it to PATH.");
  }

  const std::string graphicsApi =
      environment_.variable("DEMI_GRAPHICS_API").value_or("automatic");
  constexpr std::array accepted{"automatic", "vulkan", "opengl", "gles",
                                "noop"};
  if (std::ranges::find(accepted, graphicsApi) == accepted.end())
    add(diagnostics, Severity::Error, "DOCTOR_GRAPHICS_API_INVALID",
        "DEMI_GRAPHICS_API has an unsupported value: " + graphicsApi, {},
        "Use automatic, vulkan, opengl, gles, or noop.");
  else
    add(diagnostics, Severity::Info, "DOCTOR_GRAPHICS_API_VALID",
        "Graphics backend selection is " + graphicsApi + ".");

  if (request.platform == "android") {
    const auto sdk =
        environment_.variable("ANDROID_HOME")
            .value_or(environment_.variable("ANDROID_SDK_ROOT").value_or(""));
    const auto ndk =
        environment_.variable("ANDROID_NDK_HOME")
            .value_or(environment_.variable("CMAKE_ANDROID_NDK").value_or(""));
    if (sdk.empty() || !std::filesystem::is_directory(sdk))
      add(diagnostics, Severity::Error, "DOCTOR_ANDROID_SDK_MISSING",
          "Android SDK could not be found.", sdk,
          "Set ANDROID_HOME or ANDROID_SDK_ROOT.");
    else
      add(diagnostics, Severity::Info, "DOCTOR_ANDROID_SDK_FOUND",
          "Android SDK is available.", sdk);
    if (ndk.empty() || !std::filesystem::is_directory(ndk))
      add(diagnostics, Severity::Error, "DOCTOR_ANDROID_NDK_MISSING",
          "Android NDK could not be found.", ndk,
          "Set ANDROID_NDK_HOME or CMAKE_ANDROID_NDK.");
    else
      add(diagnostics, Severity::Info, "DOCTOR_ANDROID_NDK_FOUND",
          "Android NDK is available.", ndk);
    if (!environment_.commandAvailable("java"))
      add(diagnostics, Severity::Error, "DOCTOR_JAVA_MISSING",
          "Java is required for Android packaging.");
  }

  const auto generated =
      projectDirectory / "generated" / "runtime-cook" / request.platform;
  if (std::filesystem::exists(generated) &&
      !std::filesystem::is_regular_file(generated / "cook.manifest.json"))
    add(diagnostics, Severity::Warning, "DOCTOR_STALE_COOK_OUTPUT",
        "Cook output exists without a cook manifest.", generated,
        "Remove the directory and run `demi cook` again.");

  return diagnostics;
}

int runDoctorCommand(const std::vector<std::string> &args, std::ostream &out,
                     std::ostream &error) {
  const auto project = projectFileFrom(args);
  if (project.empty()) {
    error << "doctor requires --project <project> or a demi.project.json in "
             "the current directory.\n";
    return 2;
  }
  const std::string platform = valueAfter(args, "--platform");
  const std::string format = valueAfter(args, "--format");
  const Diagnostics diagnostics = DoctorService{}.inspect(
      {.projectPath = project,
       .platform = platform.empty() ? "linux" : platform});
  if (format.empty() || format == "text")
    printDiagnosticsText(out, diagnostics);
  else if (format == "json")
    printDiagnosticsJson(out, diagnostics);
  else {
    error << "doctor --format must be text or json.\n";
    return 2;
  }
  return hasErrors(diagnostics) ? 1 : 0;
}

} // namespace demi::cli::doctor
