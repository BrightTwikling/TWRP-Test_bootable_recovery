#include "stdio_adapter.h"

#include <cstdarg>
#include <string_view>
#include <vector>

namespace {
void print(gui2_console::severity level, const char* format, va_list arguments) {
  if (format == nullptr) return;
  va_list measure;
  va_copy(measure, arguments);
  const int length = std::vsnprintf(nullptr, 0, format, measure);
  va_end(measure);
  if (length < 0) return;
  std::vector<char> buffer(static_cast<size_t>(length) + 1);
  std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
  gui2_console::write_raw(level, std::string(buffer.data(), static_cast<size_t>(length)));
}
}  // namespace

void gui_print(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  print(gui2_console::severity::normal, format, arguments);
  va_end(arguments);
}

void gui_print_color(const char* color, const char* format, ...) {
  const std::string_view name = color == nullptr ? "normal" : color;
  const auto level = name == "error" ? gui2_console::severity::error :
                     name == "warning" ? gui2_console::severity::warning :
                     name == "highlight" ? gui2_console::severity::highlight :
                                           gui2_console::severity::normal;
  va_list arguments;
  va_start(arguments, format);
  print(level, format, arguments);
  va_end(arguments);
}

void gui_set_FILE(FILE* output) {
  gui2_console::set_output(output);
}

std::string gui_lookup(const std::string& key, const std::string& fallback) {
  return gui2_console::text(key, fallback);
}
