#ifndef GUI2_CONSOLE_STDIO_ADAPTER_H
#define GUI2_CONSOLE_STDIO_ADAPTER_H

#include <stdio.h>
#ifdef __cplusplus
#include <string>
#include "gui2/console/messages.h"
extern "C" {
#endif
void gui_print(const char* format, ...);
void gui_print_color(const char* color, const char* format, ...);
void gui_set_FILE(FILE* output);
#ifdef __cplusplus
}
// Recovery's dynamic partition-label lookup; message output uses GUI2 directly.
std::string gui_lookup(const std::string& key, const std::string& fallback);
#endif
#endif
