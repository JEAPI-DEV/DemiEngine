#include "editor/EditorJsonDocument.h"

#include <utility>

namespace demi::editor {
namespace {

std::string decodePointerToken(std::string token) {
  for (std::size_t index = 0; index + 1 < token.size();) {
    if (token[index] == '~' && token[index + 1] == '1')
      token.replace(index, 2, "/");
    else if (token[index] == '~' && token[index + 1] == '0')
      token.replace(index, 2, "~");
    else
      ++index;
  }
  return token;
}

} // namespace

bool EditorJsonDocument::open(std::filesystem::path path,
                              EditorJsonValidator validator,
                              std::string &error) {
  std::string text;
  FileRevision revision;
  if (!store_.read(path, text, revision, error))
    return false;
  try {
    nlohmann::json document = nlohmann::json::parse(text);
    Diagnostics diagnostics =
        validator ? validator(path, document) : Diagnostics{};
    path_ = std::move(path);
    validator_ = std::move(validator);
    revision_ = revision;
    document_ = std::move(document);
    savedCanonical_ = document_.dump();
    diagnostics_ = std::move(diagnostics);
    undo_.clear();
    redo_.clear();
    return true;
  } catch (const nlohmann::json::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorJsonDocument::set(const std::string_view pointer,
                             nlohmann::json value, std::string &error) {
  try {
    nlohmann::json replacement = document_;
    replacement[nlohmann::json::json_pointer(std::string(pointer))] =
        std::move(value);
    return commit(std::move(replacement), error);
  } catch (const nlohmann::json::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorJsonDocument::erase(const std::string_view pointer,
                               std::string &error) {
  try {
    const std::string value(pointer);
    const std::size_t separator = value.find_last_of('/');
    if (separator == std::string::npos || separator == 0) {
      error = "A document root cannot be removed.";
      return false;
    }
    nlohmann::json replacement = document_;
    nlohmann::json &parent = replacement.at(
        nlohmann::json::json_pointer(value.substr(0, separator)));
    const std::string token = decodePointerToken(value.substr(separator + 1));
    if (parent.is_object())
      parent.erase(token);
    else if (parent.is_array()) {
      const std::size_t index = std::stoul(token);
      if (index >= parent.size()) {
        error = "Array index is outside the document.";
        return false;
      }
      parent.erase(parent.begin() +
                   static_cast<nlohmann::json::difference_type>(index));
    } else {
      error = "Only object members and array items can be removed.";
      return false;
    }
    return commit(std::move(replacement), error);
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool EditorJsonDocument::replace(nlohmann::json document, std::string &error) {
  return commit(std::move(document), error);
}

bool EditorJsonDocument::undo(std::string &error) {
  if (undo_.empty()) {
    error = "There is no document edit to undo.";
    return false;
  }
  Change change = std::move(undo_.back());
  undo_.pop_back();
  document_ = change.before;
  diagnostics_ = validator_ ? validator_(path_, document_) : Diagnostics{};
  redo_.push_back(std::move(change));
  return true;
}

bool EditorJsonDocument::redo(std::string &error) {
  if (redo_.empty()) {
    error = "There is no document edit to redo.";
    return false;
  }
  Change change = std::move(redo_.back());
  redo_.pop_back();
  document_ = change.after;
  diagnostics_ = validator_ ? validator_(path_, document_) : Diagnostics{};
  undo_.push_back(std::move(change));
  return true;
}

bool EditorJsonDocument::save(std::string &error) {
  FileRevision replacement;
  if (store_.writeIfUnchanged(path_, document_.dump(2) + '\n', revision_,
                              replacement,
                              error) != DocumentWriteStatus::Written)
    return false;
  revision_ = replacement;
  savedCanonical_ = document_.dump();
  return true;
}

bool EditorJsonDocument::isDirty() const {
  return document_.dump() != savedCanonical_;
}

bool EditorJsonDocument::commit(nlohmann::json replacement,
                                std::string &error) {
  if (replacement == document_)
    return true;
  Diagnostics diagnostics =
      validator_ ? validator_(path_, replacement) : Diagnostics{};
  if (hasErrors(diagnostics)) {
    error = diagnostics.front().message;
    diagnostics_ = std::move(diagnostics);
    return false;
  }
  undo_.push_back({.before = document_, .after = replacement});
  redo_.clear();
  document_ = std::move(replacement);
  diagnostics_ = std::move(diagnostics);
  return true;
}

} // namespace demi::editor
