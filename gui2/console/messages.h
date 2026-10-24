#ifndef GUI2_CONSOLE_MESSAGES_H
#define GUI2_CONSOLE_MESSAGES_H

#include <concepts>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "gui2/backend/console_backend.h"
#include "gui2/i18n/i18n.h"

namespace gui2_console {

enum class severity { normal, highlight, warning, error };
// A formatter owns its argument tuple; history supplies the current translation.
using formatter = std::function<std::optional<std::string>(std::string_view)>;

struct line {
  std::string text;
  severity level = severity::normal;
  size_t line_index = 0;
  size_t offset = 0;
  bool continued = false;
};
using gui2_backend::console_cursor;
using gui2_backend::console_read_limits;
using gui2_backend::console_read_result;

console_read_result fetch_bounded(const console_cursor& cursor,
    const console_read_limits& limits, std::vector<line>* lines);

// Translated patterns follow snprintf's argument types and default promotions.
std::string render_message(gui2_i18n::language_id language, std::string_view key,
                           const formatter& render);
std::string text(std::string_view key, std::string_view fallback = {});

void set_language(gui2_i18n::language_id language);
gui2_i18n::language_id language();
void set_output(FILE* output);
void emit(severity level, std::string key, formatter render);
void write_raw(severity level, std::string text);
// Retains raw and localized entries in their original order. Language changes
// invalidate only rendered lines; stored keys and owned arguments survive.
size_t fetch(size_t from, std::vector<line>* lines);

namespace detail {
template<class> inline constexpr bool unsupported_argument = false;

template<class T>
auto capture(T&& value) {
  using value_type = std::remove_cvref_t<T>;
  if constexpr (std::same_as<value_type, char>) {
    return std::string(1, value);
  } else if constexpr (std::is_enum_v<value_type>) {
    return static_cast<std::underlying_type_t<value_type>>(value);
  } else if constexpr (std::is_arithmetic_v<value_type>) {
    // Keep int/unsigned/long/long long distinct for the printf ABI.
    return value;
  } else if constexpr (std::is_pointer_v<value_type> &&
                       std::convertible_to<T, std::string_view>) {
    return value == nullptr ? std::string{} : std::string(std::string_view(value));
  } else if constexpr (std::convertible_to<T, std::string_view>) {
    return std::string(std::string_view(value));
  } else if constexpr (requires { { value.string() } -> std::convertible_to<std::string>; }) {
    return std::string(value.string());
  } else {
    static_assert(unsupported_argument<T>, "Unsupported console message argument");
  }
}

inline const char* printf_argument(const std::string& value) {
  return value.c_str();
}

template<class T>
T printf_argument(T value) {
  static_assert(std::is_arithmetic_v<T>);
  return value;
}

template<class... Args>
formatter bind_arguments(Args&&... args) {
  return [values = std::make_tuple(capture(std::forward<Args>(args))...)]
         (std::string_view pattern) -> std::optional<std::string> {
    // A view may be a substring. snprintf requires a NUL-terminated format.
    const std::string format(pattern);
    return std::apply([&](const auto&... items) -> std::optional<std::string> {
      // printf ignores surplus arguments. The trailing zero also keeps dynamic
      // patterns with an empty tuple compatible with -Wformat-security.
      const int length = std::snprintf(nullptr, 0, format.c_str(), printf_argument(items)..., 0);
      if (length < 0) return std::nullopt;
      std::vector<char> buffer(static_cast<size_t>(length) + 1);
      const int written = std::snprintf(buffer.data(), buffer.size(), format.c_str(),
                                        printf_argument(items)..., 0);
      if (written != length) return std::nullopt;
      return std::string(buffer.data(), static_cast<size_t>(written));
    }, values);
  };
}
}  // namespace detail

template<class... Args>
std::string format(std::string_view key, Args&&... args) {
  const auto render = detail::bind_arguments(std::forward<Args>(args)...);
  return render_message(language(), key, render);
}

template<class... Args>
void write(severity level, std::string_view key, Args&&... args) {
  emit(level, std::string(key), detail::bind_arguments(std::forward<Args>(args)...));
}
template<class... Args>
void info(std::string_view key, Args&&... args) {
  write(severity::normal, key, std::forward<Args>(args)...);
}
template<class... Args>
void warning(std::string_view key, Args&&... args) {
  write(severity::warning, key, std::forward<Args>(args)...);
}
template<class... Args>
void log_error(std::string_view key, Args&&... args) {
  write(severity::error, key, std::forward<Args>(args)...);
}
template<class... Args>
void highlight(std::string_view key, Args&&... args) {
  write(severity::highlight, key, std::forward<Args>(args)...);
}
}  // namespace gui2_console

#endif
