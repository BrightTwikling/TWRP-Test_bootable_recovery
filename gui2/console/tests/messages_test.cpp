#include "gui2/console/messages.h"
#include "gui2/i18n/i18n.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <thread>

namespace {
using gui2_console::severity;
using gui2_i18n::language_id;

TEST(ConsoleFormat, StringsIntegersPercentAndPositions) {
  const auto render = gui2_console::detail::bind_arguments(std::string("/data"), 7);
  ASSERT_TRUE(render("%s: %d%%").has_value());
  EXPECT_EQ(*render("%s: %d%%"), "/data: 7%");
  // POSIX positional conversions are supplied by the host/Bionic snprintf.
  EXPECT_EQ(*render("%2$d%% %1$s %1$s"), "7% /data /data");
  EXPECT_EQ(*gui2_console::detail::bind_arguments('!')("%s"), "!");
  EXPECT_EQ(*gui2_console::detail::bind_arguments()("100%%"), "100%");
}

TEST(ConsoleFormat, OwnsArgumentsAndPreservesIntegerAbiTypes) {
  std::string value = "temporary";
  const auto render = gui2_console::detail::bind_arguments(
      value, std::numeric_limits<long long>::min(),
      std::numeric_limits<unsigned long long>::max());
  value.assign("changed");
  ASSERT_TRUE(render("%s %lld %llu").has_value());
  EXPECT_EQ(*render("%s %lld %llu"),
            "temporary -9223372036854775808 18446744073709551615");
  EXPECT_EQ(*gui2_console::detail::bind_arguments("%n {1} %%")("%s"), "%n {1} %%");
  static_assert(std::same_as<decltype(gui2_console::detail::capture(1)), int>);
  static_assert(std::same_as<decltype(gui2_console::detail::capture(1u)), unsigned>);
  static_assert(std::same_as<decltype(gui2_console::detail::capture(1L)), long>);
  static_assert(std::same_as<decltype(gui2_console::detail::capture(1ULL)), unsigned long long>);
}

TEST(ConsoleFormat, SupportsStandardWidthsPrecisionAndUnterminatedViews) {
  const auto render = gui2_console::detail::bind_arguments(std::string("abcdef"), 7, 1.25);
  EXPECT_EQ(*render("%.3s %04d %.2f"), "abc 0007 1.25");
  const std::string pattern = "%s ignored";
  EXPECT_EQ(*gui2_console::detail::bind_arguments("substring")(
                std::string_view(pattern.data(), 2)), "substring");
  const auto long_value = std::string(8192, 'x');
  EXPECT_EQ(*gui2_console::detail::bind_arguments(long_value)("%s"), long_value);
  const char* null_value = nullptr;
  EXPECT_EQ(*gui2_console::detail::bind_arguments(null_value)("[%s]"), "[]");
}

TEST(ConsoleFormat, ReportsLibraryFormattingFailureAndUnknownKeys) {
  const gui2_console::formatter failure = [](std::string_view) {
    return std::optional<std::string>{};
  };
  EXPECT_EQ(gui2_console::render_message(language_id::ENGLISH, "done", failure),
            "[message formatting failed: done]");
  EXPECT_EQ(gui2_console::format("unknown-key"), "[unknown message: unknown-key]");
}

TEST(ConsoleCatalogue, BindingsReadTheExistingLanguagePacks) {
  const auto bindings = gui2_i18n::message_bindings();
  ASSERT_FALSE(bindings.empty());
  for (auto language : gui2_i18n::kLanguageValues) {
    const auto& pack = gui2_i18n::get_language_pack(language);
    for (size_t index = 0; index < bindings.size(); ++index) {
      const auto& binding = bindings[index];
      const char* value = pack.*binding.member;
      ASSERT_NE(value, nullptr) << binding.key;
      EXPECT_NE(*value, '\0') << binding.key;
      EXPECT_EQ(gui2_i18n::lookup_message(language, binding.key), value);
      if (index != 0) EXPECT_LT(bindings[index - 1].key, binding.key);
    }
  }
  const auto& english = gui2_i18n::get_language_pack(language_id::ENGLISH);
  EXPECT_EQ(gui2_i18n::lookup_message(static_cast<language_id>(999), "restore"),
            english.restore);
  EXPECT_EQ(gui2_i18n::lookup_message(language_id::ZH_CN, "unable_to_mount"),
            gui2_i18n::get_language_pack(language_id::ZH_CN).unable_to_mount);
  EXPECT_TRUE(gui2_i18n::lookup_message(language_id::ENGLISH, "unknown-key").empty());
}

TEST(ConsoleCatalogue, BackupSentinelsSurviveLanguageChanges) {
  gui2_console::set_language(language_id::ZH_CN);
  EXPECT_TRUE(gui2_i18n::matches_message("curr_date", "(Current Date)"));
  EXPECT_TRUE(gui2_i18n::matches_message("curr_date", "（当前日期）"));
  EXPECT_TRUE(gui2_i18n::matches_message("auto_generate", "(Auto Generate)"));
  EXPECT_FALSE(gui2_i18n::matches_message("auto_generate", "my-backup"));
  gui2_console::set_language(language_id::ENGLISH);
}

TEST(ConsoleCatalogue, FormatsMigratedAndPreviouslyMissingMessages) {
  gui2_console::set_language(language_id::ENGLISH);
  EXPECT_EQ(gui2_console::format("install_took_seconds_msg", 12), "Install took 12 second(s).");
  EXPECT_EQ(gui2_console::format("backup_name_invalid", "backup?", '?'),
            "Backup name 'backup?' contains invalid character: '?'");
  EXPECT_EQ(gui2_console::format("size_progress", "1 MiB", "2 MiB", 50),
            "1 MiB / 2 MiB, 50%");
  EXPECT_EQ(gui2_console::format("file_progress", 1ULL, 2ULL, 50), "1 of 2 files, 50%");
  EXPECT_EQ(gui2_console::format("zip_compatible_err"), "Zip Treble compatibility error!");
}

TEST(ConsoleHistory, LanguageChangesPreserveRawLinesOrderAndSeverity) {
  gui2_console::set_language(language_id::ENGLISH);
  const size_t from = gui2_console::fetch(0, nullptr);
  std::string path = "/owned.zip";
  gui2_console::info("installing_zip", path);
  path.assign("/changed.zip");
  gui2_console::write_raw(severity::warning, "raw %s\nsecond line\n");
  gui2_console::log_error("unable_to_mount", "/data");
  std::vector<gui2_console::line> english;
  EXPECT_EQ(gui2_console::fetch(from, &english), from + 4);
  ASSERT_EQ(english.size(), 4u);
  EXPECT_EQ(english[0].text, "Installing zip file '/owned.zip'");
  EXPECT_EQ(english[1].text, "raw %s");
  EXPECT_EQ(english[2].text, "second line");
  EXPECT_EQ(english[1].level, severity::warning);
  EXPECT_EQ(english[3].level, severity::error);
  gui2_console::set_language(language_id::ZH_CN);
  std::vector<gui2_console::line> chinese;
  gui2_console::fetch(from, &chinese);
  ASSERT_EQ(chinese.size(), 4u);
  EXPECT_NE(chinese[0].text, english[0].text);
  EXPECT_NE(chinese[0].text.find("/owned.zip"), std::string::npos);
  EXPECT_EQ(chinese[1].text, english[1].text);
  EXPECT_EQ(chinese[2].text, english[2].text);
  EXPECT_EQ(chinese[3].level, severity::error);
  gui2_console::set_language(language_id::ENGLISH);
}

TEST(ConsoleOutput, MirrorsOnceAndDoesNotReplayOnLanguageChange) {
  FILE* mirror = std::tmpfile();
  ASSERT_NE(mirror, nullptr);
  gui2_console::set_language(language_id::ENGLISH);
  gui2_console::set_output(mirror);
  gui2_console::info("unable_to_mount", "/mirror");
  const long end = std::ftell(mirror);
  gui2_console::set_language(language_id::ZH_CN);
  gui2_console::fetch(0, nullptr);
  EXPECT_EQ(std::ftell(mirror), end);
  gui2_console::set_output(nullptr);
  std::rewind(mirror);
  char buffer[256] = {};
  const size_t count = std::fread(buffer, 1, sizeof(buffer), mirror);
  EXPECT_EQ(std::string(buffer, count), "Unable to mount /mirror\n");
  std::fclose(mirror);
  gui2_console::set_language(language_id::ENGLISH);
}

TEST(ConsoleHistory, ConcurrentWorkersRetainAllMessages) {
  gui2_console::set_language(language_id::ENGLISH);
  const size_t from = gui2_console::fetch(0, nullptr);
  {
    std::vector<std::jthread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) {
      workers.emplace_back([worker] {
        for (int index = 0; index < 20; ++index)
          gui2_console::info("split_thread", worker, index);
      });
    }
  }
  std::vector<gui2_console::line> lines;
  EXPECT_EQ(gui2_console::fetch(from, &lines), from + 80);
  EXPECT_EQ(lines.size(), 80u);
}
TEST(ConsoleHistory, BoundedReadsPreserveUtf8AndLineOffsets) {
  gui2_console::set_language(language_id::ENGLISH);
  const auto end = gui2_console::fetch_bounded({}, {0, 0, 10000}, nullptr);
  gui2_console::console_cursor cursor{end.cursor.generation, end.total, 0};
  const std::string message = "prefix 中文 😀 suffix";
  gui2_console::write_raw(severity::warning, message + "\n\nlast\n");
  std::vector<gui2_console::line> pieces;
  std::string joined;
  size_t offset = 0;
  bool finished = false;
  for (int batch = 0; batch < 50; ++batch) {
    pieces.clear();
    const auto result = gui2_console::fetch_bounded(cursor, {1, 5, 1}, &pieces);
    ASSERT_FALSE(result.busy);
    ASSERT_LE(pieces.size(), 1u);
    for (const auto& piece : pieces) {
      EXPECT_LE(piece.text.size(), 5u);
      if (!piece.text.empty())
        EXPECT_NE(static_cast<unsigned char>(piece.text[0]) & 0xc0, 0x80);
      EXPECT_EQ(piece.offset, offset);
      EXPECT_EQ(piece.level, severity::warning);
      joined += piece.text;
      offset += piece.text.size();
      if (!piece.continued) {
        joined += '\n';
        offset = 0;
      }
    }
    cursor = result.cursor;
    if (!result.has_more) {
      finished = true;
      break;
    }
  }
  EXPECT_TRUE(finished);
  EXPECT_EQ(joined, message + "\n\nlast\n");
}

TEST(ConsoleHistory, BoundedReadsHonorZeroBudgetAndLanguageReset) {
  gui2_console::set_language(language_id::ENGLISH);
  const auto end = gui2_console::fetch_bounded({}, {0, 0, 10000}, nullptr);
  gui2_console::console_cursor cursor{end.cursor.generation, end.total, 0};
  gui2_console::info("done");
  std::vector<gui2_console::line> lines;
  const auto pending = gui2_console::fetch_bounded(cursor, {1, 4, 0}, &lines);
  EXPECT_TRUE(pending.has_more);
  EXPECT_EQ(pending.cursor.line, cursor.line);
  EXPECT_TRUE(lines.empty());
  const auto too_small = gui2_console::fetch_bounded(cursor, {1, 3, 1}, &lines);
  EXPECT_EQ(too_small.cursor.line, cursor.line);
  EXPECT_EQ(too_small.cursor.offset, 0u);
  EXPECT_TRUE(lines.empty());
  gui2_console::set_language(language_id::ZH_CN);
  const auto reset = gui2_console::fetch_bounded(cursor, {0, 0, 0}, &lines);
  EXPECT_NE(reset.cursor.generation, cursor.generation);
  EXPECT_EQ(reset.cursor.line, 0u);
  EXPECT_EQ(reset.cursor.offset, 0u);
  EXPECT_TRUE(reset.has_more);
  gui2_console::set_language(language_id::ENGLISH);
}

}  // namespace
