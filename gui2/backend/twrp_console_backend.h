#ifndef GUI2_BACKEND_TWRP_CONSOLE_BACKEND_H
#define GUI2_BACKEND_TWRP_CONSOLE_BACKEND_H

#include <cstddef>
#include <string>
#include <vector>

#include "console_backend.h"

namespace gui2_backend {

class twrp_console_backend final : public console_backend {
 public:
  size_t fetch(size_t from, std::vector<console_line>* lines) override;
  console_read_result fetch_bounded(const console_cursor& cursor,
                                    const console_read_limits& limits,
                                    std::vector<console_line>* lines) override;
  void set_language(const std::string& language) override;
  void retranslate(const std::string& language) override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_CONSOLE_BACKEND_H
