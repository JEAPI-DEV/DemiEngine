#include "demi/filesystem/ProjectPaths.h"
#include "demi/schema/Validation.h"
#include "demi/runtime/ui/RichTextParser.h"
#include "demi/runtime/ui/TextEditingEngine.h"
#include "demi/runtime/ui/TextLayoutEngine.h"
#include "demi/runtime/ui/UiMutationQueue.h"
#include "demi/runtime/ui/UiVirtualCollection.h"
#include "demi/runtime/ui/UiTweenSystem.h"
#include "demi/runtime/ui/UiLocalization.h"
#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/ui/UiStateController.h"
#include "demi/runtime/ui/UiPrefabResolver.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <ranges>

using namespace demi::runtime::ui;

int main() {
  TextLayoutEngine text;
  const auto wrapped = text.layout({.text = "alpha beta gamma delta epsilon",
                                    .width = 66.0F,
                                    .fontSize = 10.0F,
                                    .wrap = TextWrap::Word,
                                    .horizontal = TextHorizontalAlignment::Center,
                                    .overflow = TextOverflow::Ellipsis,
                                    .maxLines = 2});
  if (wrapped.lines.size() != 2 || !wrapped.truncated ||
      wrapped.lines.back().text.find("...") == std::string::npos ||
      wrapped.lines.front().x <= 0.0F) {
    std::cerr << "Text wrapping, alignment, or ellipsis failed.\n";
    return 1;
  }
  const auto selection = TextLayoutEngine::selectionRects(wrapped, 1, 4);
  if (selection.empty() || selection[0].width <= 0.0F ||
      TextLayoutEngine::hitTest(wrapped, {0.0F, 0.0F}) > 1) {
    std::cerr << "Text selection geometry or hit testing failed.\n";
    return 1;
  }

  const std::string emoji = "A\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"
                            "e\xCC\x81";
  if (TextLayoutEngine::graphemeCount(emoji) != 3 ||
      TextLayoutEngine::graphemeSlice(emoji, 1, 1) !=
          "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" ||
      TextLayoutEngine::graphemeSlice("\xFF", 0, 1).has_value()) {
    std::cerr << "Unicode grapheme boundaries or invalid UTF-8 handling failed.\n";
    return 1;
  }
  const auto unicode = text.layout({.text = "مرحبا", .width = 100.0F,
                                    .fontSize = 12.0F});
  if (!unicode.validUtf8 || unicode.shapingComplete) {
    std::cerr << "Complex text did not expose incomplete shaping honestly.\n";
    return 1;
  }
  const auto emptyLayout = text.layout(
      {.text = {}, .width = 100.0F, .height = 30.0F, .fontSize = 12.0F});
  if (emptyLayout.carets.size() != 1 ||
      emptyLayout.carets.front().grapheme != 0) {
    std::cerr << "Empty editable text did not expose caret geometry.\n";
    return 1;
  }

  TextEditState editing;
  std::string edited = "Ae\xCC\x81"
                       "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
  TextEditingEngine::normalize(edited, editing);
  if (editing.caret != 3 || !TextEditingEngine::backspace(edited, editing) ||
      edited != "Ae\xCC\x81" || !TextEditingEngine::backspace(edited, editing) ||
      edited != "A" || editing.caret != 1) {
    std::cerr << "Backspace split a Unicode grapheme cluster.\n";
    return 1;
  }
  TextEditingEngine::moveTo(editing, edited, 0);
  if (!TextEditingEngine::deleteForward(edited, editing) || !edited.empty() ||
      TextEditingEngine::deleteForward(edited, editing)) {
    std::cerr << "Forward deletion did not respect text boundaries.\n";
    return 1;
  }
  edited = "alpha";
  editing = {};
  TextEditingEngine::selectAll(editing, edited);
  if (!TextEditingEngine::insert(edited, editing, "β") || edited != "β" ||
      editing.caret != 1) {
    std::cerr << "Selection replacement failed.\n";
    return 1;
  }
  const std::string preserved = edited;
  if (TextEditingEngine::insert(edited, editing, "\xFF") ||
      edited != preserved) {
    std::cerr << "Invalid UTF-8 mutated an editable value.\n";
    return 1;
  }
  TextEditingEngine::selectAll(editing, edited);
  if (!TextEditingEngine::setComposition(editing, "候補", 1, 99) ||
      edited != "β" || TextEditingEngine::displayText(edited, editing) != "候補" ||
      editing.compositionSelectionLength != 1 ||
      TextEditingEngine::displayCaret(edited, editing) != 2) {
    std::cerr << "IME composition did not remain separate from committed text.\n";
    return 1;
  }
  if (!TextEditingEngine::insert(edited, editing, "確定") || edited != "確定" ||
      !editing.composition.empty()) {
    std::cerr << "IME commit did not replace the selected text.\n";
    return 1;
  }
  TextEditingEngine::selectAll(editing, edited);
  TextEditingEngine::move(editing, edited, -1);
  if (editing.caret != 0 || editing.anchor != 0) {
    std::cerr << "Arrow movement did not collapse selection to its edge.\n";
    return 1;
  }
  TextLayoutCache cache(2);
  (void)cache.layout({.text = "one", .width = 20.0F});
  (void)cache.layout({.text = "one", .width = 20.0F});
  (void)cache.layout({.text = "two", .width = 20.0F});
  (void)cache.layout({.text = "three", .width = 20.0F});
  if (cache.stats().hits != 1 || cache.stats().misses != 3 ||
      cache.stats().entries != 2) {
    std::cerr << "Text layout cache was not bounded or deterministic.\n";
    return 1;
  }

  const auto rich = RichTextParser{}.parse(
      "Use [strong]safe[/strong] [link=item:1]content[/link].");
  if (rich.text != "Use safe content." || rich.spans.size() != 2 ||
      !rich.diagnostics.empty()) {
    std::cerr << "Allowlisted rich text parsing failed.\n";
    return 1;
  }
  const auto hostile = RichTextParser{}.parse("[lua=evil]x[/lua]");
  if (hostile.diagnostics.size() != 2 || hostile.text != "x") {
    std::cerr << "Executable or unknown rich tags were not rejected.\n";
    return 1;
  }

  UiDocument document;
  document.nodes = {{.id = "root", .type = "container"},
                    {.id = "template", .parent = "root", .type = "button",
                     .focusable = true},
                    {.id = "label", .parent = "template", .type = "label"}};
  document.nodes[1].hovered = true;
  document.nodes[1].textEdit.composition = "transient";
  UiMutationQueue::initializeGenerations(document);
  const auto source = UiMutationQueue::handle(document, "template");
  if (!source) return 1;
  UiMutationQueue clone;
  clone.clone(*source, "row_1", "root");
  if (!clone.apply(document).applied || document.nodes.size() != 5 ||
      !UiMutationQueue::handle(document, "row_1.label") ||
      document.nodes[3].hovered ||
      !document.nodes[3].textEdit.composition.empty()) {
    std::cerr << "Transactional subtree cloning failed.\n";
    return 1;
  }

  const auto row = *UiMutationQueue::handle(document, "row_1");
  document.focusedId = row.id;
  document.pointerCaptureId = row.id;
  document.pointerCaptures[9] = row.id;
  UiMutationQueue remove;
  remove.remove(row);
  if (!remove.apply(document).applied || !document.focusedId.empty() ||
      !document.pointerCaptureId.empty() || !document.pointerCaptures.empty() ||
      UiMutationQueue::alive(document, row)) {
    std::cerr << "Removal did not cancel focus/capture or invalidate handles.\n";
    return 1;
  }

  const auto root = *UiMutationQueue::handle(document, "root");
  const std::size_t before = document.nodes.size();
  UiMutationQueue invalid;
  invalid.create("root", {.id = "valid", .type = "label"});
  invalid.create("missing", {.id = "orphan", .type = "label"});
  if (invalid.apply(document).applied || document.nodes.size() != before ||
      UiMutationQueue::handle(document, "valid")) {
    std::cerr << "Failed multi-node mutation was not transactional.\n";
    return 1;
  }
  UiMutationQueue cycle;
  cycle.reparent(root, "label");
  if (cycle.apply(document).applied) {
    std::cerr << "Cycle-producing reparent was accepted.\n";
    return 1;
  }

  UiTweenSystem tweens;
  const auto label = *UiMutationQueue::handle(document, "label");
  const auto tween = tweens.start(document, label, UiTweenProperty::PositionX,
                                  100.0F, 1.0F);
  tweens.update(document, 0.5F);
  if (!tween || document.nodes[2].layout.position.x != 50.0F ||
      tweens.activeCount() != 1) {
    std::cerr << "UI tween did not update the live generation.\n";
    return 1;
  }
  UiMutationQueue removeLabel;
  removeLabel.remove(label);
  (void)removeLabel.apply(document);
  tweens.update(document, 0.1F);
  if (tweens.activeCount() != 0) {
    std::cerr << "UI tween survived target removal.\n";
    return 1;
  }
  UiDocument localized;
  localized.locales["en"] = {{"title", "Start"}};
  localized.locales["de"] = {{"title", "Starten"}};
  localized.nodes.push_back({.id = "title", .type = "label",
                             .localizationKey = "title"});
  localized.nodes[0].textEdit = {.caret = 50,
                                 .anchor = 40,
                                 .composition = "unfinished",
                                 .initialized = true};
  std::string localeError;
  if (!UiLocalization{}.setLocale(localized, "de", localeError) ||
      localized.nodes[0].text != "Starten" ||
      !localized.nodes[0].textEdit.composition.empty() ||
      localized.nodes[0].textEdit.caret != 7 ||
      UiLocalization{}.setLocale(localized, "missing", localeError) ||
      localized.nodes[0].text != "Starten") {
    std::cerr << "Locale switching did not preserve the last valid UI.\n";
    return 1;
  }
  UiLocalization{}.setPseudoLocale(localized, true);
  if (localized.nodes[0].text.front() != '[') {
    std::cerr << "Pseudo-localization did not invalidate localized text.\n";
    return 1;
  }
  UiDocument hidden;
  hidden.nodes = {{.id = "panel", .type = "panel"},
                  {.id = "control", .parent = "panel", .type = "button",
                   .focusable = true}};
  hidden.nodes[1].textEdit.composition = "unfinished";
  hidden.focusedId = "control";
  hidden.pointerCaptures[4] = "control";
  if (!UiStateController{}.setVisible(hidden, "panel", false) ||
      !hidden.focusedId.empty() || !hidden.pointerCaptures.empty() ||
      !hidden.nodes[1].textEdit.composition.empty()) {
    std::cerr << "Hiding an ancestor retained descendant interaction state.\n";
    return 1;
  }

  const auto range = UiVirtualCollection::visibleRange(10000, 20.0F, 1000.0F,
                                                        200.0F, 3);
  if (range.first != 47 || range.count != 16 || range.count >= 10000 ||
      UiVirtualCollection::visibleRange(10, 0.0F, 0.0F, 100.0F).count != 0) {
    std::cerr << "Bounded virtual collection range failed.\n";
    return 1;
  }

  const std::filesystem::path prefabRoot =
      std::filesystem::temp_directory_path() / "demi_ui_prefab_step3";
  std::filesystem::remove_all(prefabRoot);
  std::filesystem::create_directories(prefabRoot / "ui");
  std::filesystem::create_directories(prefabRoot / "scenes");
  const auto write = [](const std::filesystem::path &path,
                        const std::string_view content) {
    std::ofstream output(path);
    output << content;
    return output.good();
  };
  if (!write(prefabRoot / "demi.project.json", R"({"format_version":1})") ||
      !write(prefabRoot / "ui/badge.ui.prefab.json", R"({
        "format_version": 1,
        "id": "ui-prefab://badge",
        "parameters": {"caption": {"type": "string", "default": "NEW"}},
        "root": {"id": "badge", "type": "panel", "children": [
          {"id": "caption", "type": "label", "text": "${caption}"}
        ]}
      })") ||
      !write(prefabRoot / "ui/button.ui.prefab.json", R"({
        "format_version": 1,
        "id": "ui-prefab://button",
        "parameters": {
          "label": {"type": "string"},
          "size": {"type": "number", "default": 20}
        },
        "root": {"id": "root", "type": "button", "text": "Open ${label}",
          "font_size": "${size}", "children": [
            {"id": "label", "type": "label", "text": "${label}"},
            {"id": "status", "prefab": "ui-prefab://badge", "arguments": {}}
          ]}
      })")) {
    std::cerr << "Could not create UI prefab fixtures.\n";
    return 1;
  }
  const std::filesystem::path hudPath = prefabRoot / "scenes/test.hud.json";
  const nlohmann::json prefabHud = nlohmann::json::parse(R"({
    "format_version": 1,
    "root": {"id": "menu", "type": "panel", "children": [
      {"id": "confirm", "prefab": "ui-prefab://button",
       "arguments": {"label": "Settings", "size": 24}}
    ]}
  })");
  const auto expandedPrefab = expandUiDocument(hudPath, prefabHud);
  if (!expandedPrefab.document || !expandedPrefab.diagnostics.empty()) {
    std::cerr << "Valid parameterized UI prefab did not expand.\n";
    return 1;
  }
  const UiDocument prefabDocument = parseUiDocument(*expandedPrefab.document);
  const auto confirm = std::ranges::find(prefabDocument.nodes, "confirm",
                                         &UiNode::id);
  const auto nested = std::ranges::find(prefabDocument.nodes,
                                        "confirm.status.caption", &UiNode::id);
  if (confirm == prefabDocument.nodes.end() ||
      confirm->text != "Open Settings" || confirm->fontSize != 24.0F ||
      nested == prefabDocument.nodes.end() || nested->text != "NEW") {
    std::cerr << "UI prefab parameters or stable nested ids were incorrect.\n";
    return 1;
  }
  nlohmann::json invalidArguments = prefabHud;
  invalidArguments["root"]["children"][0]["arguments"] = {
      {"label", 7}, {"unknown", true}};
  const auto rejectedArguments = expandUiDocument(hudPath, invalidArguments);
  if (rejectedArguments.document || rejectedArguments.diagnostics.size() < 2) {
    std::cerr << "Invalid UI prefab arguments were not rejected transactionally.\n";
    return 1;
  }
  nlohmann::json ambiguousNode = prefabHud;
  ambiguousNode["root"]["children"][0]["type"] = "button";
  const auto rejectedAmbiguousNode = expandUiDocument(hudPath, ambiguousNode);
  if (rejectedAmbiguousNode.document ||
      std::ranges::none_of(rejectedAmbiguousNode.diagnostics,
                           [](const auto &item) {
                             return item.code == "UI_PREFAB_NODE_AMBIGUOUS";
                           })) {
    std::cerr << "Ambiguous typed/prefab UI node was not rejected.\n";
    return 1;
  }
  if (!write(prefabRoot / "ui/bad_version.ui.prefab.json", R"({
        "format_version":"one","id":"ui-prefab://bad_version",
        "root":{"id":"root","type":"panel"}
      })")) {
    return 1;
  }
  const auto malformedVersion =
      inspectUiPrefab(prefabRoot / "ui/bad_version.ui.prefab.json");
  if (malformedVersion.document ||
      std::ranges::none_of(malformedVersion.diagnostics, [](const auto &item) {
        return item.code == "UI_PREFAB_DOCUMENT_INVALID";
      })) {
    std::cerr << "Malformed UI prefab version did not fail safely.\n";
    return 1;
  }
  if (!write(prefabRoot / "ui/cycle_a.ui.prefab.json", R"({
        "format_version":1,"id":"ui-prefab://cycle_a",
        "root":{"id":"b","prefab":"ui-prefab://cycle_b","arguments":{}}
      })") ||
      !write(prefabRoot / "ui/cycle_b.ui.prefab.json", R"({
        "format_version":1,"id":"ui-prefab://cycle_b",
        "root":{"id":"a","prefab":"ui-prefab://cycle_a","arguments":{}}
      })")) {
    return 1;
  }
  nlohmann::json cyclicHud = prefabHud;
  cyclicHud["root"]["children"] = nlohmann::json::array(
      {{{"id", "cycle"}, {"prefab", "ui-prefab://cycle_a"},
        {"arguments", nlohmann::json::object()}}});
  const auto rejectedCycle = expandUiDocument(hudPath, cyclicHud);
  if (rejectedCycle.document ||
      std::ranges::none_of(rejectedCycle.diagnostics, [](const auto &item) {
        return item.code == "UI_PREFAB_CYCLE";
      }) ||
      resolveUiPrefabReference(hudPath, "ui-prefab://../outside")) {
    std::cerr << "UI prefab cycle or traversal protection failed.\n";
    return 1;
  }
  if (!demi::isUiPrefabFile(prefabRoot / "ui/button.ui.prefab.json") ||
      demi::isPrefabFile(prefabRoot / "ui/button.ui.prefab.json") ||
      demi::classifySourceFile(prefabRoot / "ui/button.ui.prefab.json") !=
          demi::SourceFileKind::UiPrefab) {
    std::cerr << "UI prefab source classification was ambiguous.\n";
    return 1;
  }
  const auto validPrefabDiagnostics = demi::validateTextFile(
      prefabRoot / "ui/button.ui.prefab.json",
      demi::SourceFileKind::UiPrefab);
  if (demi::hasErrors(validPrefabDiagnostics)) {
    std::cerr << "Valid UI prefab failed CLI-source validation.\n";
    return 1;
  }
  invalidArguments["format_version"] = 1;
  if (!write(hudPath, invalidArguments.dump(2))) {
    return 1;
  }
  const auto invalidHudDiagnostics =
      demi::validateTextFile(hudPath, demi::SourceFileKind::Hud);
  if (std::ranges::none_of(invalidHudDiagnostics, [](const auto &item) {
        return item.code == "UI_PREFAB_ARGUMENT_UNKNOWN";
      }) ||
      std::ranges::none_of(invalidHudDiagnostics, [](const auto &item) {
        return item.code == "UI_PREFAB_ARGUMENT_TYPE";
      })) {
    std::cerr << "HUD validation did not surface UI prefab argument errors.\n";
    return 1;
  }
  std::filesystem::remove_all(prefabRoot);
  return 0;
}
