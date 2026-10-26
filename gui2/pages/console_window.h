#ifndef GUI2_PAGES_CONSOLE_WINDOW_H
#define GUI2_PAGES_CONSOLE_WINDOW_H

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "backend/console_backend.h"

namespace gui2_pages {

// Per-poll work and retained-row limits for progress logs.
struct console_window_limits {
  // Long lines are split at UTF-8 boundaries before creating labels.
  size_t max_rows = 160;
  size_t row_bytes = 512;
  // One backend read: entries, text bytes and translated messages.
  size_t read_lines = 24;
  size_t read_bytes = 4096;
  size_t read_messages = 64;
  // History pages are collected across polls and applied in batches.
  size_t apply_rows = 32;
  size_t page_rows = 96;
  // Opening a page starts near the latest output.
  size_t tail_lines = 160;
};

// Rows are identified by (line_index, offset).
using console_window_row = gui2_backend::console_line;

// Both ends are visible when the content fits the viewport.
struct console_window_viewport {
  bool at_top = true;
  bool at_bottom = true;
};

// Deltas preserve console order; generation resets also delete in batches.
struct console_window_delta {
  size_t dropped_from_top = 0;
  size_t dropped_from_bottom = 0;
  std::vector<console_window_row> prepended;
  std::vector<console_window_row> appended;
};

struct console_window_frame {
  console_window_delta delta;
};

enum class console_window_page_kind {
  NONE,
  UP,
  DOWN,
};

// Collect history without changing the view until the page is ready.
struct console_window_page {
  console_window_page_kind kind = console_window_page_kind::NONE;
  // Still walking the console; the collected rows are not applied yet.
  bool collecting = false;
  gui2_backend::console_cursor agent{};
  // The first visible row bounds an upward read.
  gui2_backend::console_cursor seam{};
  // A down page that ended at the newest line.
  bool reaches_tail = false;
  std::vector<console_window_row> collected;
};

struct console_window {
  // Mirror of the view, oldest first, at most limits.max_rows rows.
  std::deque<console_window_row> rows;
  // Where the next read starts while following the printer.
  gui2_backend::console_cursor tail{};
  size_t generation = 0;
  bool started = false;
  bool follow = true;
  // Nothing newer than rows.back() was readable at the last look.
  bool at_tail = false;
  // Read-ahead rows awaiting the next apply_rows batch.
  std::deque<console_window_row> backlog;
  // Old-generation labels awaiting batched deletion.
  size_t clearing = 0;
  console_window_page page;
};

inline console_window_limits install_console_window_limits() {
  return console_window_limits{};
}

// Arm only after submitting a frame for the same page instance.
inline bool console_window_frame_arms(bool presented, bool rendered_install_page, bool same_page) {
  return presented && rendered_install_page && same_page;
}

inline bool console_window_can_read(bool armed, bool has_body) {
  return armed && has_body;
}

namespace console_window_detail {

inline bool is_utf8_continuation(char byte) {
  return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

// Preserve UTF-8 boundaries; invalid sequences must still advance.
inline size_t utf8_prefix_bytes(const std::string& text, size_t start, size_t max_bytes) {
  const size_t available = text.size() - start;
  if (max_bytes >= available) return available;

  const size_t end = start + max_bytes;
  if (!is_utf8_continuation(text[end])) return max_bytes;

  for (size_t back = 1; back < 4 && back <= max_bytes; back++) {
    if (!is_utf8_continuation(text[end - back])) return max_bytes - back;
  }
  return max_bytes;
}

inline void push_rows(const console_window_limits& limits, const gui2_backend::console_line& entry,
                      std::vector<console_window_row>* rows) {
  if (entry.text.empty()) {
    rows->push_back(entry);
    return;
  }
  const size_t budget = limits.row_bytes < 4 ? 4 : limits.row_bytes;
  size_t local = 0;
  while (local < entry.text.size()) {
    size_t take = utf8_prefix_bytes(entry.text, local, budget);
    if (take == 0) take = entry.text.size() - local;
    console_window_row row;
    row.text = entry.text.substr(local, take);
    row.severity = entry.severity;
    row.line_index = entry.line_index;
    row.offset = entry.offset + local;
    row.continued = entry.continued || local + take < entry.text.size();
    rows->push_back(std::move(row));
    local += take;
  }
}

inline bool cursor_before(const gui2_backend::console_cursor& left,
                          const gui2_backend::console_cursor& right) {
  return left.line < right.line || (left.line == right.line && left.offset < right.offset);
}

inline gui2_backend::console_cursor row_cursor(const console_window_row& row) {
  return {0, row.line_index, row.offset};
}

inline gui2_backend::console_read_limits read_limits_for(const console_window_limits& limits) {
  return {limits.read_lines, limits.read_bytes, limits.read_messages};
}

// Continue within a split line, otherwise advance past it (including blank lines).
inline gui2_backend::console_cursor cursor_after_row(const console_window_row& row,
                                                    size_t generation) {
  gui2_backend::console_cursor cursor;
  cursor.generation = generation;
  if (row.continued) {
    cursor.line = row.line_index;
    cursor.offset = row.offset + row.text.size();
  } else {
    cursor.line = row.line_index + 1;
    cursor.offset = 0;
  }
  return cursor;
}

// Reset to the new tail; old view labels are removed by later polls.
inline void start_at_tail(console_window* window, const console_window_limits& limits,
                          const gui2_backend::console_read_result& read) {
  window->clearing += window->rows.size();
  window->generation = read.cursor.generation;
  window->started = true;
  window->rows.clear();
  window->backlog.clear();
  window->page = console_window_page{};
  window->follow = true;
  window->at_tail = false;
  const size_t start = read.total > limits.tail_lines ? read.total - limits.tail_lines : 0;
  window->tail.generation = window->generation;
  window->tail.line = start;
  window->tail.offset = 0;
}

// Keep the window and view delta trimmed to the same newest rows.
inline void push_back_rows(console_window* window, const console_window_limits& limits,
                           std::vector<console_window_row> rows, console_window_delta* delta) {
  const size_t before = window->rows.size();
  delta->appended = std::move(rows);
  for (const auto& row : delta->appended) window->rows.push_back(row);
  if (window->rows.size() <= limits.max_rows) return;

  const size_t excess = window->rows.size() - limits.max_rows;
  const size_t from_view = std::min(excess, before);
  delta->dropped_from_top += from_view;
  window->rows.erase(window->rows.begin(), window->rows.begin() + excess);
  if (excess > from_view)
    delta->appended.erase(delta->appended.begin(), delta->appended.begin() + (excess - from_view));
}

inline void drain_backlog(console_window* window, const console_window_limits& limits,
                          console_window_frame* frame) {
  if (window->backlog.empty()) return;
  const size_t take = std::min(limits.apply_rows, window->backlog.size());
  std::vector<console_window_row> rows(window->backlog.begin(), window->backlog.begin() + take);
  window->backlog.erase(window->backlog.begin(), window->backlog.begin() + take);
  push_back_rows(window, limits, std::move(rows), &frame->delta);
}

inline console_window_frame follow_step(console_window* window, const console_window_limits& limits,
                                        gui2_backend::console_backend* console) {
  console_window_frame frame;
  if (!window->backlog.empty()) {
    // The last read is still owed to the view: no read this poll.
    drain_backlog(window, limits, &frame);
    return frame;
  }

  std::vector<gui2_backend::console_line> entries;
  const auto read = console->fetch_bounded(window->tail, read_limits_for(limits), &entries);
  if (read.busy) return frame;
  if (read.cursor.generation != window->generation) {
    start_at_tail(window, limits, read);
    return frame;
  }

  window->tail = read.cursor;
  // has_more includes untranslated messages.
  window->at_tail = !read.has_more && read.cursor.line >= read.total;

  std::vector<console_window_row> rows;
  for (const auto& entry : entries) push_rows(limits, entry, &rows);
  for (auto& row : rows) window->backlog.push_back(std::move(row));
  drain_backlog(window, limits, &frame);
  return frame;
}

inline void begin_up_page(console_window* window, const console_window_limits& limits) {
  if (window->rows.empty()) return;
  const console_window_row& front = window->rows.front();
  // Reads only move forward: walk from earlier lines to the visible seam.
  const size_t start =
      front.line_index > limits.page_rows ? front.line_index - limits.page_rows : 0;
  if (start == front.line_index && front.offset == 0) return;  // nothing above

  console_window_page page;
  page.kind = console_window_page_kind::UP;
  page.collecting = true;
  page.seam.generation = window->generation;
  page.seam.line = front.line_index;
  page.seam.offset = front.offset;
  page.agent.generation = window->generation;
  page.agent.line = start;
  page.agent.offset = 0;
  window->page = std::move(page);
}

inline void begin_down_page(console_window* window) {
  if (window->rows.empty()) return;
  console_window_page page;
  page.kind = console_window_page_kind::DOWN;
  page.collecting = true;
  page.agent = cursor_after_row(window->rows.back(), window->generation);
  window->page = std::move(page);
}

// Walks the console with one bounded read per call until the page is complete.
inline void collect_page(console_window* window, const console_window_limits& limits,
                         gui2_backend::console_backend* console) {
  console_window_page& page = window->page;
  std::vector<gui2_backend::console_line> entries;
  const auto read = console->fetch_bounded(page.agent, read_limits_for(limits), &entries);
  if (read.busy) return;  // keep the page as it is and try again next poll
  if (read.cursor.generation != window->generation) {
    start_at_tail(window, limits, read);
    return;
  }

  std::vector<console_window_row> rows;
  for (const auto& entry : entries) push_rows(limits, entry, &rows);
  if (page.kind == console_window_page_kind::UP) {
    // Stop at the seam: the rows from it on are already on screen.
    for (auto& row : rows) {
      if (!cursor_before(row_cursor(row), page.seam)) continue;
      if (row.line_index == page.seam.line && row.offset + row.text.size() > page.seam.offset) {
        // Trim at the seam to avoid repeating visible bytes.
        row.text.resize(page.seam.offset - row.offset);
        row.continued = true;
      }
      page.collected.push_back(std::move(row));
    }
    if (page.collected.size() > limits.page_rows)
      page.collected.erase(page.collected.begin(),
                           page.collected.begin() + (page.collected.size() - limits.page_rows));
  } else {
    for (auto& row : rows) page.collected.push_back(std::move(row));
    // Keep overflow from this read: its cursor already passed these rows.
    if (page.collected.size() >= limits.page_rows) page.collecting = false;
  }

  page.agent = read.cursor;
  if (page.kind == console_window_page_kind::DOWN && !read.has_more) {
    page.reaches_tail = true;
    page.collecting = false;
  } else if (entries.empty()) {
    // No progress: stop before a walk can spin on a stale cursor.
    page.collecting = false;
  } else if (page.kind == console_window_page_kind::UP &&
             !cursor_before(page.agent, page.seam)) {
    page.collecting = false;
  }
  if (!page.collecting && page.collected.empty()) {
    // An empty down page can still establish that the visible rows reached the tail.
    if (page.kind == console_window_page_kind::DOWN && page.reaches_tail) {
      window->at_tail = true;
      if (!window->rows.empty())
        window->tail = cursor_after_row(window->rows.back(), window->generation);
    }
    window->page = console_window_page{};
  }
}

// Apply the rows nearest to the visible window first.
inline void apply_page_batch(console_window* window, const console_window_limits& limits,
                             console_window_frame* frame) {
  console_window_page& page = window->page;
  if (page.collected.empty()) {
    window->page = console_window_page{};
    return;
  }
  const size_t take = std::min(limits.apply_rows, page.collected.size());
  if (page.kind == console_window_page_kind::UP) {
    const size_t from = page.collected.size() - take;
    frame->delta.prepended.assign(page.collected.begin() + from, page.collected.end());
    page.collected.erase(page.collected.begin() + from, page.collected.end());
    for (auto it = frame->delta.prepended.rbegin(); it != frame->delta.prepended.rend(); ++it)
      window->rows.push_front(*it);
    // Browsing older rows must not resume following at the window bottom.
    window->at_tail = false;
    while (window->rows.size() > limits.max_rows) {
      window->rows.pop_back();
      frame->delta.dropped_from_bottom++;
    }
  } else {
    std::vector<console_window_row> rows(page.collected.begin(), page.collected.begin() + take);
    page.collected.erase(page.collected.begin(), page.collected.begin() + take);
    push_back_rows(window, limits, std::move(rows), &frame->delta);
  }
  if (page.collected.empty()) {
    if (page.kind == console_window_page_kind::DOWN) {
      window->at_tail = page.reaches_tail;
      // Resume after the last displayed row, not the old follow cursor.
      if (page.reaches_tail && !window->rows.empty())
        window->tail = cursor_after_row(window->rows.back(), window->generation);
    }
    window->page = console_window_page{};
  }
}

inline console_window_frame browse_step(console_window* window, const console_window_limits& limits,
                                        const console_window_viewport& viewport,
                                        gui2_backend::console_backend* console) {
  console_window_frame frame;

  // Probe generation while browsing; an active page read already does this.
  if (!window->page.collecting) {
    gui2_backend::console_cursor probe = window->tail;
    probe.line = std::numeric_limits<size_t>::max();
    probe.offset = 0;
    const auto read = console->fetch_bounded(probe, gui2_backend::console_read_limits{}, nullptr);
    if (read.busy) return frame;
    if (read.cursor.generation != window->generation) {
      start_at_tail(window, limits, read);
      return frame;
    }
  }

  if (window->page.kind == console_window_page_kind::NONE) {
    // Start paging only at a scrollable edge; defer its read to the next poll.
    if (viewport.at_top && !viewport.at_bottom)
      begin_up_page(window, limits);
    else if (viewport.at_bottom && !viewport.at_top && !window->at_tail)
      begin_down_page(window);
    return frame;
  }

  const size_t generation = window->generation;
  if (window->page.collecting) collect_page(window, limits, console);
  if (window->generation != generation) return frame;
  if (!window->page.collecting) apply_page_batch(window, limits, &frame);
  return frame;
}

}  // namespace console_window_detail

// One poll: at most one backend read and at most one batch of rows for the view.
inline console_window_frame console_window_step(console_window* window,
                                                const console_window_limits& limits,
                                                const console_window_viewport& viewport,
                                                gui2_backend::console_backend* console) {
  using namespace console_window_detail;
  console_window_frame frame;
  if (window == nullptr || console == nullptr) return frame;

  if (!window->started) {
    // Probe length without copying text before choosing the tail cursor.
    gui2_backend::console_cursor probe;
    probe.generation = window->generation;
    probe.line = std::numeric_limits<size_t>::max();
    probe.offset = 0;
    const auto read = console->fetch_bounded(probe, read_limits_for(limits), nullptr);
    if (read.busy) return frame;
    start_at_tail(window, limits, read);
    return frame;
  }

  if (window->clearing > 0) {
    // Finish deleting old-generation labels before reading or drawing new rows.
    const size_t take = std::min(window->clearing, limits.apply_rows);
    frame.delta.dropped_from_top = take;
    window->clearing -= take;
    return frame;
  }

  if (window->follow) {
    // Scrolling away suspends following; an empty view resumes after reset.
    if (!viewport.at_bottom && !window->rows.empty()) {
      window->follow = false;
      // Paging will reread these rows from the last displayed position.
      if (!window->backlog.empty()) {
        window->backlog.clear();
        window->at_tail = false;
      }
    }
  } else if (window->rows.empty() ||
             (viewport.at_bottom && (window->at_tail || viewport.at_top))) {
    window->follow = true;
  }

  if (window->follow) return follow_step(window, limits, console);
  return browse_step(window, limits, viewport, console);
}

inline void console_window_reset(console_window* window) {
  if (window == nullptr) return;
  *window = console_window{};
}

}  // namespace gui2_pages

#endif  // GUI2_PAGES_CONSOLE_WINDOW_H
