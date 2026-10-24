#ifndef GUI2_BACKEND_CONSOLE_BACKEND_H
#define GUI2_BACKEND_CONSOLE_BACKEND_H

#include <cstddef>
#include <string>
#include <vector>

namespace gui2_backend {

enum class console_severity {
  NORMAL,
  HIGHLIGHT,
  WARNING,
  ERROR,
};

struct console_line {
  std::string text;
  console_severity severity = console_severity::NORMAL;
  // Long lines share line_index; offset is in bytes and continued marks a partial line.
  size_t line_index = 0;
  size_t offset = 0;
  bool continued = false;
};

// Reuse the returned cursor to preserve UTF-8 boundaries. A generation change
// means the console was rebuilt; discard previously displayed lines.
struct console_cursor {
  size_t generation = 0;
  size_t line = 0;
  size_t offset = 0;
};

// Zero disables that work; max_bytes below 4 disables copying.
struct console_read_limits {
  size_t max_lines = 0;
  size_t max_bytes = 0;
  size_t max_messages = 0;
};

struct console_read_result {
  console_cursor cursor;
  size_t total = 0;
  bool has_more = false;  // Unread lines or pending messages remain.
  // Lock unavailable: only busy and the unchanged input cursor are meaningful.
  bool busy = false;
};

// Recovery console backed by GUI2 i18n and retained typed message records.
class console_backend {
 public:
  virtual ~console_backend() = default;

  // Appends the lines recorded after `from` and returns the new total count.
  virtual size_t fetch(size_t from, std::vector<console_line>* lines) = 0;

  // Appends a bounded batch without waiting for the console lock. Long lines
  // are returned in pieces; resume from the returned cursor on the next frame.
  virtual console_read_result fetch_bounded(const console_cursor& cursor,
                                            const console_read_limits& limits,
                                            std::vector<console_line>* lines) = 0;

  virtual void set_language(const std::string& language) = 0;
  virtual void retranslate(const std::string& language) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_CONSOLE_BACKEND_H
