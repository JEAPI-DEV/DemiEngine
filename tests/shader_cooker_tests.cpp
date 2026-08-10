#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetHash.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#ifndef DEMI_SHADERC_PATH
#define DEMI_SHADERC_PATH ""
#endif
#ifndef DEMI_BGFX_SHADER_INCLUDE_DIR
#define DEMI_BGFX_SHADER_INCLUDE_DIR ""
#endif

namespace {

void write(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

nlohmann::json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_shader_cooker_" + std::to_string(nonce));
  const auto project = root / "project";
  const auto shaders = project / "assets/shaders/simple";
  const auto definition = shaders / "varying.def.sc";
  const auto vertex = shaders / "simple.vs.sc";
  const auto fragment = shaders / "simple.fs.sc";
  const auto source = shaders / "simple.shader.json";

  write(project / "demi.project.json",
        R"({"format_version":1,"name":"Shader Cook Fixture","scenes":[]})");
  write(definition,
        "vec4 v_color0 : COLOR0;\n"
        "vec3 a_position : POSITION;\n"
        "vec4 a_color0 : COLOR0;\n");
  write(vertex,
        "$input a_position, a_color0\n"
        "$output v_color0\n"
        "#include <bgfx_shader.sh>\n"
        "void main() {\n"
        "  gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));\n"
        "  v_color0 = a_color0;\n"
        "}\n");
  write(fragment,
        "$input v_color0\n"
        "#include <bgfx_shader.sh>\n"
        "void main() { gl_FragColor = v_color0; }\n");
  write(source,
        R"({"format_version":1,"vertex":"simple.vs.sc","fragment":"simple.fs.sc","varying":"varying.def.sc"})");

  const auto sourceHash =
      demi::assets::hashFiles({fragment, source, vertex, definition});
  assert(sourceHash);
  write(shaders / "simple.shader.asset.json",
        nlohmann::json({{"format_version", 1},
                        {"id", "asset://shaders/simple"},
                        {"type", "Shader"},
                        {"importer", "shader"},
                        {"importer_version", 1},
                        {"source", "simple.shader.json"},
                        {"source_hash", *sourceHash},
                        {"dependencies", nlohmann::json::array()},
                        {"settings", nlohmann::json::object()}})
            .dump(2));

  for (const std::string platform : {"linux", "android"}) {
    const auto output = root / ("cooked-" + platform);
    const demi::Diagnostics diagnostics = demi::assets::cookProject(
        {.projectFile = project / "demi.project.json",
         .outputDirectory = output,
         .platform = platform,
         .shaderCompiler = DEMI_SHADERC_PATH,
         .shaderIncludeDirectory = DEMI_BGFX_SHADER_INCLUDE_DIR});
    assert(!demi::hasErrors(diagnostics));
    const auto manifest = readJson(output / "cook.manifest.json");
    assert(manifest["shader_programs"].size() == 2);
    std::set<std::string> backends;
    for (const auto &program : manifest["shader_programs"]) {
      assert(program["asset"] == "asset://shaders/simple");
      backends.insert(program["backend"].get<std::string>());
      assert(std::filesystem::is_regular_file(
          output / program["vertex"].get<std::string>()));
      assert(std::filesystem::is_regular_file(
          output / program["fragment"].get<std::string>()));
    }
    if (platform == "linux")
      assert(backends == std::set<std::string>({"opengl", "vulkan"}));
    else
      assert(backends == std::set<std::string>({"opengles", "vulkan"}));
  }

  std::filesystem::remove_all(root);
  return 0;
}
