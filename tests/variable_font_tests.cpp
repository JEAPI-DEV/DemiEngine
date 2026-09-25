#include "demi/assets/FontAssetSettings.h"
#include "demi/assets/GeneratedAtlasCooker.h"
#include "demi/runtime/render/backend/DefaultFont.h"
#include "demi/runtime/ui/FontRasterizer.h"
#include "demi/runtime/ui/TextShaper.h"
#include "editor/EditorFontLoader.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <numeric>
#include <stdexcept>

using namespace demi;
using namespace demi::runtime;
void check(bool ok, const std::string &error) {
  if (!ok)
    throw std::runtime_error(error);
}
int main() {
  try {
    const auto bytes = render::defaultFontData();
    ui::FontRasterizer regular, bold, optical;
    std::string error;
    check(regular.open(bytes, {{"wght", 400}}, error), error);
    check(bold.open(bytes, {{"wght", 700}}, error), error);
    check(optical.open(bytes, {{"wght", 400}, {"opsz", 32}}, error), error);
    check(regular.axes().size() == 2, "Inter axes were not discovered");
    ui::RasterizedGlyph a, b, c;
    check(regular.rasterize(regular.glyphIndex('M'), 48, a, error), error);
    check(bold.rasterize(bold.glyphIndex('M'), 48, b, error), error);
    check(optical.rasterize(optical.glyphIndex('M'), 48, c, error), error);
    check(std::accumulate(b.coverage.begin(), b.coverage.end(), 0ULL) >
              std::accumulate(a.coverage.begin(), a.coverage.end(), 0ULL),
          "Bold axis did not increase actual ink coverage");
    check(a.coverage != c.coverage, "Optical size did not change outlines");
    check(!regular.open(bytes, {{"wght", 9999}}, error),
          "Out-of-range axis accepted");
    check(!regular.open(bytes, {{"nope", 500}}, error),
          "Unknown axis accepted");
    ui::RasterizedGlyph retained;
    check(regular.rasterize(regular.glyphIndex('M'), 48, retained, error) &&
              retained.coverage == a.coverage,
          "Failed font replacement mutated the face");
    ui::FontResolver fonts, other;
    check(fonts.add("face", bytes, 1, error, false, {{"wght", 400}}), error);
    check(other.add("face", bytes, 1, error, false, {{"wght", 700}}), error);
    check(fonts.revision() != other.revision(),
          "Variation instances alias layout cache identity");
    const auto measured=ui::TextShaper{}.shape({.text="M",.fontSize=48},fonts);
    check(std::abs(measured.advance-a.advance)<.05F,"Shaping and rasterization use different font sizes");
    const auto light = ui::TextShaper{}.shape(
        {.text = "Hamburgefonts", .fontSize = 48}, fonts);
    const auto heavy = ui::TextShaper{}.shape(
        {.text = "Hamburgefonts", .fontSize = 48}, other);
    check(light.complete && heavy.complete && light.advance != heavy.advance,
          "Shaping ignored variation coordinates");
    AssetManifest asset{
        .type = "Font2D",
        .settingsJson = R"({"variations":{"wght":500,"opsz":24}})",
        .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                      "fonts/Inter/Inter-VariableFont_opsz,wght.ttf"};
    check(!hasErrors(assets::validateFontAssetSettings(asset)),
          "Valid font settings rejected");
    asset.settingsJson = R"({"variations":{"wght":9999}})";
    check(hasErrors(assets::validateFontAssetSettings(asset)),
          "Invalid asset axis was not diagnosed");
    const auto output=std::filesystem::temp_directory_path()/
        ("demi-variable-font-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(output),"Could not create owned cook fixture");
    asset.type="FontAtlas2D";
    asset.settingsJson=R"({"variations":{"wght":400},"glyph_ranges":["U+004D-U+004D"]})";
    const auto regularAtlas=assets::cookGeneratedAtlas(asset,{},output/"regular");
    asset.settingsJson=R"({"variations":{"wght":700},"glyph_ranges":["U+004D-U+004D"]})";
    const auto boldAtlas=assets::cookGeneratedAtlas(asset,{},output/"bold");
    check(!hasErrors(regularAtlas.diagnostics) && !hasErrors(boldAtlas.diagnostics),"Variable atlas cook failed");
    const auto page=[](const auto &outputs) {
      for(const auto &path:outputs) if(path.extension()==".png") {
        std::ifstream file(path,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
      }
      return std::string{};
    };
    check(!page(regularAtlas.outputs).empty() && page(regularAtlas.outputs)!=page(boldAtlas.outputs),"Cook ignored font weight");
    std::filesystem::remove_all(output);

    editor::EditorFontLoader lightLoader({{"wght", 400}}),
        heavyLoader({{"wght", 700}});
    ImGui::SetAllocatorFunctions([](std::size_t size,void *){return std::malloc(size);},
                                [](void *memory,void *){std::free(memory);});
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.FontLoader = lightLoader.loader();
    auto *lightFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<std::byte *>(bytes.data()), static_cast<int>(bytes.size()),
        32, &config);
    config.FontLoader = heavyLoader.loader();
    auto *heavyFont = io.Fonts->AddFontFromMemoryTTF(
        const_cast<std::byte *>(bytes.data()), static_cast<int>(bytes.size()),
        32, &config);
    check(lightFont && heavyFont,
          "Editor variation font initialization failed");
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    check(pixels && width > 0 && height > 0, "Editor variable atlas is empty");
    const auto la = lightFont->GetFontBaked(32)->FindGlyph('M')->AdvanceX;
    const auto ha = heavyFont->GetFontBaked(32)->FindGlyph('M')->AdvanceX;
    check(la != ha, "Editor loader ignored weight");
    check(lightFont->GetFontBaked(48)->FindGlyph('M')->AdvanceX > la,
          "Editor loader ignored changed size");
    ImGui::DestroyContext();
    std::cout << "Variable font axes, outlines, shaping, validation and editor "
                 "loading passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
