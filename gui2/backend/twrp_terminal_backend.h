#ifndef GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H
#define GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H

#include <cstddef>
#include <string>

#include "terminal_backend.h"

namespace gui2_backend {

// Adapts the recovery PTY engine to GUI2 terminal pages.
class twrp_terminal_backend final : public terminal_backend {
 public:
  bool start() override;
  bool running() override;
  bool alternate_screen() override;
  bool foreground_job() override;
  int cursor_x() override;
  int cursor_y() override;
  void stop() override;
  void set_size(int columns, int rows) override;
  void pump() override;
  int update_counter() override;
  size_t line_count() override;
  std::string line(size_t index) override;
  void send_line(const std::string& text) override;
  void send_byte(char byte) override;
  void send_char(int codepoint) override;
  void send_key(int key) override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_TERMINAL_BACKEND_H
