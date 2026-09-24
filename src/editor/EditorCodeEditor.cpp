#include "editor/EditorCodeEditor.h"
#include "cli/LuaStubExport.h"
#include "editor/EditorDocumentStore.h"
#include <nlohmann/json.hpp>
namespace demi::editor {
std::vector<std::string> codeEditorCommand(const EditorPreferences &preferences,
                                           const std::filesystem::path &project,
                                           const std::filesystem::path &file) {
  std::vector<std::string> command{preferences.codeEditor};
  for (auto argument : preferences.codeEditorArguments) {
    for (const auto &[token, replacement] :
         std::vector<std::pair<std::string, std::string>>{
             {"{project}", std::filesystem::absolute(project).string()},
             {"{file}", std::filesystem::absolute(file).string()}}) {
      std::size_t start = 0;
      while ((start = argument.find(token, start)) != std::string::npos) {
        argument.replace(start, token.size(), replacement);
        start += replacement.size();
      }
    }
    command.push_back(std::move(argument));
  }
  return command;
}
bool prepareCodeEditorWorkspace(const std::filesystem::path &project,
                                const std::filesystem::path &stubSource,
                                std::string &error) {
  if (!cli::exportLuaStubs(stubSource, project / ".demi/lua", error))
    return false;
  const auto settings = project / ".luarc.json";
  if (std::filesystem::exists(settings))
    return true;
  const nlohmann::json defaults{
      {"runtime.version", "Lua 5.4"},
      {"workspace.library", {".demi/lua", ".demi/packages"}}};
  return EditorDocumentStore{}.writeNew(settings, defaults.dump(2) + "\n",
                                        error);
}
} // namespace demi::editor
