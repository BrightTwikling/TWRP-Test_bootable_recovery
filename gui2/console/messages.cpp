#include "messages.h"

#include <algorithm>
#include <mutex>

#include "gui2/i18n/i18n.h"

namespace gui2_console {
namespace {

struct entry {
  severity level;
  bool localized;
  std::string content;
  formatter render;
};
struct console_state {
  std::mutex mutex;
  gui2_i18n::language_id language = gui2_i18n::language_id::ENGLISH;
  FILE* output = nullptr;
  std::vector<entry> entries;
  std::vector<line> rendered;
  size_t rendered_entries = 0;
  size_t generation = 0;
};

console_state& state() {
  // Function-local initialization also supports messages from static constructors.
  static console_state instance;
  return instance;
}

void output_locked(console_state& console, std::string_view value) {
  std::fwrite(value.data(), 1, value.size(), stdout);
  if (console.output != nullptr && console.output != stdout) {
    std::fwrite(value.data(), 1, value.size(), console.output);
    std::fflush(console.output);
  }
}

void append_lines(console_state& console, const entry& item) {
  const std::string value = item.localized
      ? render_message(console.language, item.content, item.render) : item.content;
  size_t start = 0;
  while (start < value.size()) {
    const auto end = value.find('\n', start);
    console.rendered.push_back({value.substr(start, end == std::string::npos
                                                       ? end : end - start), item.level});
    if (end == std::string::npos) break;
    start = end + 1;
  }
}

}  // namespace

std::string render_message(gui2_i18n::language_id language, std::string_view key,
                           const formatter& render) {
  const auto pattern = gui2_i18n::lookup_message(language, key);
  if (pattern.empty()) return "[unknown message: " + std::string(key) + "]";
  if (auto result = render(pattern)) return std::move(*result);
  const auto english = gui2_i18n::lookup_message(gui2_i18n::language_id::ENGLISH, key);
  if (pattern != english) {
    if (auto result = render(english)) return std::move(*result);
  }
  return "[message formatting failed: " + std::string(key) + "]";
}

void set_language(gui2_i18n::language_id selected) {
  auto& console = state();
  const std::lock_guard lock(console.mutex);
  if (console.language == selected) return;
  console.language = selected;
  ++console.generation;
  console.rendered.clear();
  console.rendered_entries = 0;
}

gui2_i18n::language_id language() {
  auto& console = state();
  const std::lock_guard lock(console.mutex);
  return console.language;
}

std::string text(std::string_view key, std::string_view fallback) {
  const auto value = gui2_i18n::lookup_message(language(), key);
  return std::string(value.empty() ? (fallback.empty() ? key : fallback) : value);
}

void set_output(FILE* output) {
  auto& console = state();
  const std::lock_guard lock(console.mutex);
  console.output = output;
}

void emit(severity level, std::string key, formatter render) {
  auto& console = state();
  const std::lock_guard lock(console.mutex);
  std::string rendered = render_message(console.language, key, render);
  if (rendered.empty() || rendered.back() != '\n') rendered.push_back('\n');
  output_locked(console, rendered);
  console.entries.push_back({level, true, std::move(key), std::move(render)});
}

void write_raw(severity level, std::string value) {
  auto& console = state();
  const std::lock_guard lock(console.mutex);
  output_locked(console, value);
  // ZIP update-binary emits standalone newlines; keep its existing display behavior.
  if (!value.empty() && value != "\n")
    console.entries.push_back({level, false, std::move(value), {}});
}

size_t fetch(size_t from, std::vector<line>* lines) {
  auto& console = state();
  const std::lock_guard lock(console.mutex);
  while (console.rendered_entries < console.entries.size())
    append_lines(console, console.entries[console.rendered_entries++]);
  if (lines != nullptr) {
    from = std::min(from, console.rendered.size());
    lines->insert(lines->end(), console.rendered.begin() + from, console.rendered.end());
  }
  return console.rendered.size();
}
namespace {

bool is_utf8_continuation(char byte)
{
  return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

// Keep UTF-8 characters whole; malformed bytes must still advance.
size_t utf8_prefix_bytes(const std::string& text, size_t start, size_t max_bytes)
{
  const size_t available = text.size() - start;
  if (max_bytes >= available)
    return available;

  const size_t end = start + max_bytes;
  if (!is_utf8_continuation(text[end]))
    return max_bytes;

  for (size_t back = 1; back < 4 && back <= max_bytes; back++) {
    if (!is_utf8_continuation(text[end - back]))
      return max_bytes - back;
  }
  return max_bytes;
}

} // namespace

console_read_result fetch_bounded(const console_cursor& cursor,
                                  const console_read_limits& limits,
                                  std::vector<line>* lines)
{
  console_read_result result;
  result.cursor = cursor;

  auto& console = state();
  std::unique_lock lock(console.mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    result.busy = true;
    return result;
  }

  result.cursor.generation = console.generation;
  if (cursor.generation != console.generation) {
    result.cursor.line = 0;
    result.cursor.offset = 0;
  }

  if (limits.max_messages > 0 && console.rendered_entries < console.entries.size()) {
    const size_t batch = std::min(console.entries.size() - console.rendered_entries, limits.max_messages);
    for (size_t m = 0; m < batch; m++)
      append_lines(console, console.entries[console.rendered_entries + m]);
    console.rendered_entries += batch;
  }

  const size_t total = console.rendered.size();
  size_t line = result.cursor.line;
  size_t offset = result.cursor.offset;
  if (line >= total) {
    line = total;
    offset = 0;
  } else {
    offset = std::min(console.rendered[line].text.size(), offset);
  }

  // Four bytes allow any UTF-8 character to advance on a fresh batch.
  const bool can_copy = lines != nullptr && limits.max_lines > 0 && limits.max_bytes >= 4;
  size_t entries = 0;
  size_t bytes = 0;

  while (can_copy && entries < limits.max_lines && bytes < limits.max_bytes && line < total) {
    const std::string& text = console.rendered[line].text;
    if (offset >= text.size() && !text.empty()) {
      line++;
      offset = 0;
      continue;
    }

    const size_t take = text.empty() ? 0 : utf8_prefix_bytes(text, offset, limits.max_bytes - bytes);
    if (take == 0 && !text.empty())
      break; // the next character needs a fresh batch's byte budget

    gui2_console::line entry;
    entry.text = text.substr(offset, take);
    entry.level = console.rendered[line].level;
    entry.line_index = line;
    entry.offset = offset;
    entry.continued = offset + take < text.size();
    const bool continued = entry.continued;
    lines->push_back(std::move(entry));

    entries++;
    bytes += take;
    if (continued) {
      offset += take;
    } else {
      line++;
      offset = 0;
    }
  }

  result.cursor.line = line;
  result.cursor.offset = offset;
  result.total = total;
  result.has_more = line < total || console.rendered_entries < console.entries.size();
  return result;
}

}  // namespace gui2_console
