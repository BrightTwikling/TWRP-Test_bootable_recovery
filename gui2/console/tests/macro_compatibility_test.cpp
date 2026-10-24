// Reproduce a function-like error macro already being defined by a C library.
#define error(...) static_assert(false, "legacy error macro must not expand")
#include "gui2/console/messages.h"

namespace {
void output_with_error_macro() {
  gui2_console::log_error("done");
}
}  // namespace
#undef error

#include <gtest/gtest.h>

TEST(ConsoleMacros, HeaderAndCallAcceptAnExistingErrorMacro) {
  gui2_console::set_language(gui2_i18n::language_id::ENGLISH);
  const size_t from = gui2_console::fetch(0, nullptr);
  output_with_error_macro();
  std::vector<gui2_console::line> lines;
  gui2_console::fetch(from, &lines);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines.front().level, gui2_console::severity::error);
}
