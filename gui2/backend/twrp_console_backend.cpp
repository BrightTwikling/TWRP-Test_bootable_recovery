#include "twrp_console_backend.h"

#include "gui2/console/messages.h"
#include "partitions.hpp"

namespace gui2_backend {
namespace {
console_severity severity_for(gui2_console::severity level) {
  switch (level) {
    case gui2_console::severity::highlight: return console_severity::HIGHLIGHT;
    case gui2_console::severity::warning: return console_severity::WARNING;
    case gui2_console::severity::error: return console_severity::ERROR;
    default: return console_severity::NORMAL;
  }
}
}  // namespace

size_t twrp_console_backend::fetch(size_t from, std::vector<console_line>* lines) {
  std::vector<gui2_console::line> messages;
  const size_t total = gui2_console::fetch(from, lines == nullptr ? nullptr : &messages);
  if (lines != nullptr) {
    lines->reserve(lines->size() + messages.size());
    for (auto& message : messages)
      lines->push_back({std::move(message.text), severity_for(message.level), from++});
  }
  return total;
}

console_read_result twrp_console_backend::fetch_bounded(const console_cursor& cursor,
    const console_read_limits& limits, std::vector<console_line>* lines) {
  std::vector<gui2_console::line> messages;
  const auto read = gui2_console::fetch_bounded(
      cursor, limits, lines == nullptr ? nullptr : &messages);
  if (lines != nullptr) {
    lines->reserve(lines->size() + messages.size());
    for (auto& message : messages)
      lines->push_back({std::move(message.text), severity_for(message.level),
                        message.line_index, message.offset, message.continued});
  }
  return read;
}

void twrp_console_backend::set_language(const std::string& language) {
  gui2_console::set_language(gui2_i18n::language_from_code(language));
}

void twrp_console_backend::retranslate(const std::string& language) {
  set_language(language);
  PartitionManager.TranslatePartitionDisplayNames();
}
}  // namespace gui2_backend
