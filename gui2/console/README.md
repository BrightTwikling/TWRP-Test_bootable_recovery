# GUI2 recovery messages

Recovery messages are members of the existing `language_pack` in
`gui2/i18n/i18n.h`. Translations live alongside UI strings in `en.cpp`,
`zh_cn.cpp`, `zh_tw.cpp` and `ru.cpp`. A single sorted key-to-member-pointer map
in `i18n.cpp` serves all languages; there are no separate message catalogues.
An unsupported language or a missing translation falls back to English. Fonts
remain runtime resources; message translation does not read XML or depend on
`TW_EXTRA_LANGUAGES`.

Append new message fields and initializers in the same order in all four language
files, then add their key-to-field bindings in `i18n.cpp`. The Python validator
checks the complete language_pack initializer count, binding/declaration order
and translation formats without requiring per-line comments. Message members use
their key names; the few names already used by distinct UI strings take a
`_message` suffix.

Use owned, typed parameters rather than a preformatted temporary string:

```cpp
#include "gui2/console/messages.h"

gui2_console::info("installing_zip", zip_path);
gui2_console::log_error("unable_to_mount", mount_point);
gui2_console::highlight("install_took_seconds_msg", elapsed_seconds);
std::string progress = gui2_console::format("size_progress", current, total, percent);
```

Messages keep their key, severity and copied arguments. The console serializes
output from worker/UI threads to stdout, an optional CLI mirror, and one ordered
history. Selecting another language invalidates rendered history only; raw
output, including installer's `ui_print`, stays unchanged. Repainting history
does not replay stdout or the CLI mirror. GUI2 rebuilds its console views after
language changes and reads history again from offset zero.

Formatting is delegated to `std::snprintf`: first measure the output, then
allocate a complete buffer and render it. Each history record owns a formatter
closure containing a copied `std::tuple`; `std::apply` supplies its arguments.
Strings, string views, paths and individual characters are owned as strings;
only at the snprintf call are they exposed as `c_str()`. Arithmetic values keep
their actual C++ types, so `int`, `unsigned`, `long` and `long long` remain
compatible with the corresponding printf conversions and default promotions.

Patterns follow the standard printf contract. Callers must match `%d`, `%u`,
`%llu`, etc. to their real argument types; there is no custom runtime format
parser or type-mismatch detector. Checked-in catalogues are validated for matching
argument positions, signedness and length modifiers in all four languages.
POSIX positional formats such as `%2$s` are handled by Bionic/host snprintf.
Never pass untrusted format strings. The C++ tests do not invoke undefined
behavior with deliberately mismatched arguments.

A negative snprintf result or inconsistent measured/written length is a
formatting failure; a failed translated template retries English. Unknown keys
produce an explicit diagnostic message. The key-based public API and formatter
closure isolate this implementation so a later switch to `std::format` can keep
message call sites and history ownership unchanged.

`gui_print`, `gui_print_color` and `gui_set_FILE` in `stdio_adapter.*` are adapters for raw printf
output and CLI integration. Dynamic recovery labels can still use `gui_lookup`,
which delegates to the same compiled catalogue. There is no Msg implementation,
translator callback or second console history behind those adapters.

Validate catalogue syntax, coverage and format signatures without compiling:

```sh
python3 gui2/console/tests/validate_catalogues.py
```

The optional host test target `gui2_console_messages_test` covers argument
ownership, integer ABI types/ranges, standard width/precision, catalogue lookup, language changes,
raw/localized output ordering, severity, CLI mirroring and concurrent writers.
It uses the console/i18n sources directly and does not link LVGL or recovery.
The host tests have been added but not built or executed as part of this change.
