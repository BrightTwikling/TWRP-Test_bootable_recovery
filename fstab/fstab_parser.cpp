/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/mount.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "fstab_parser.hpp"

namespace twrp::fstab {
namespace {

using android::base::ParseByteCount;
using android::base::ParseInt;
using android::base::ReadFileToString;
using android::base::Split;
using android::base::StartsWith;
using android::base::Tokenize;
using android::base::Trim;

struct MountFlag {
  const char* name;
  unsigned long value;
};

// The MS_* set known to AOSP libfstab; anything else in the mnt_flags column
// is a filesystem-specific option.
constexpr MountFlag kMountFlags[] = {
  {"noatime", MS_NOATIME},        {"noexec", MS_NOEXEC},
  {"nosuid", MS_NOSUID},          {"nodev", MS_NODEV},
  {"nodiratime", MS_NODIRATIME},  {"ro", MS_RDONLY},
  {"rw", 0},                      {"sync", MS_SYNCHRONOUS},
  {"remount", MS_REMOUNT},        {"bind", MS_BIND},
  {"rec", MS_REC},                {"unbindable", MS_UNBINDABLE},
  {"private", MS_PRIVATE},        {"slave", MS_SLAVE},
  {"shared", MS_SHARED},          {"lazytime", MS_LAZYTIME},
  {"nosymfollow", MS_NOSYMFOLLOW},
  {"defaults", 0},
};

struct FsMgrBoolFlag {
  const char* name;
  bool FsMgrFlags::*field;
};

// Exact-match fsmgr_flags tokens that only set a boolean.
constexpr FsMgrBoolFlag kFsMgrBoolFlags[] = {
  {"wait", &FsMgrFlags::wait},
  {"check", &FsMgrFlags::check},
  {"nonremovable", &FsMgrFlags::nonremovable},
  {"recoveryonly", &FsMgrFlags::recovery_only},
  {"noemulatedsd", &FsMgrFlags::no_emulated_sd},
  {"notrim", &FsMgrFlags::no_trim},
  {"formattable", &FsMgrFlags::formattable},
  {"slotselect", &FsMgrFlags::slot_select},
  {"slotselect_other", &FsMgrFlags::slot_select_other},
  {"latemount", &FsMgrFlags::late_mount},
  {"nofail", &FsMgrFlags::no_fail},
  {"quota", &FsMgrFlags::quota},
  {"logical", &FsMgrFlags::logical},
  {"first_stage_mount", &FsMgrFlags::first_stage_mount},
  {"fsverity", &FsMgrFlags::fs_verity},
  {"metadata_csum", &FsMgrFlags::ext_meta_csum},
  {"fscompress", &FsMgrFlags::fs_compress},
  {"overlayfs_remove_missing_lowerdir", &FsMgrFlags::overlayfs_remove_missing_lowerdir},
  {"wrappedkey", &FsMgrFlags::wrapped_key},
  {"checkpoint=block", &FsMgrFlags::checkpoint_blk},
  {"checkpoint=fs", &FsMgrFlags::checkpoint_fs},
  {"overlay=on", &FsMgrFlags::overlay_on},
  {"overlay=off", &FsMgrFlags::overlay_off},
  {"resize", &FsMgrFlags::resize},
};

struct TwrpBoolFlag {
  const char* name;
  bool TwrpFlags::*field;
};

// Exact-match twrp.flags tokens that only set a boolean; the value form is
// bare, "1" or "0".
constexpr TwrpBoolFlag kTwrpBoolFlags[] = {
    {"flashimg", &TwrpFlags::flashimg},
    {"mounttodecrypt", &TwrpFlags::mounttodecrypt},
    {"storage", &TwrpFlags::storage},
    {"wipeingui", &TwrpFlags::wipeingui},
    {"removable", &TwrpFlags::removable},
    {"canbeencrypted", &TwrpFlags::canbeencrypted},
    {"canencryptbackup", &TwrpFlags::canencryptbackup},
    {"usermrf", &TwrpFlags::usermrf},
};

int64_t CalculateZramSize(int percentage) {
  int64_t total = sysconf(_SC_PHYS_PAGES);
  total *= percentage;
  total /= 100;
  return total * sysconf(_SC_PAGESIZE);
}

void ParseMountFlags(const std::string& flags, FstabEntry* entry) {
  std::string fs_options;
  for (const auto& flag : Split(flags, ",")) {
    bool known = false;
    for (const auto& [name, value] : kMountFlags) {
      if (flag == name) {
        entry->flags |= value;
        known = true;
        break;
      }
    }
    if (known) continue;

    // Unknown here means a filesystem-specific option, not an error.
    if (!fs_options.empty()) fs_options.append(",");
    fs_options.append(flag);

    if (const auto eq = flag.find('='); eq != std::string::npos) {
      const auto arg = flag.substr(eq + 1);
      if (entry->fs_type == "f2fs" && StartsWith(flag, "reserve_root=")) {
        int64_t blocks;
        if (ParseInt(arg, &blocks, static_cast<int64_t>(0),
                     std::numeric_limits<int64_t>::max() >> 12)) {
          entry->reserved_size = blocks << 12;
        } else {
          LOG(WARNING) << "fstab: malformed reserve_root=: " << arg;
        }
      } else if (StartsWith(flag, "lowerdir=")) {
        entry->lowerdir = arg;
      }
    }
  }
  entry->fs_options = std::move(fs_options);
}

void ParseFsMgrFlags(const std::string& flags, const size_t line_no, FstabEntry* entry) {
  for (const auto& flag : Split(flags, ",")) {
    if (flag.empty() || flag == "defaults") continue;

    bool handled = false;
    for (const auto& [name, field] : kFsMgrBoolFlags) {
      if (flag == name) {
        entry->fs_mgr_flags.*field = true;
        handled = true;
        break;
      }
    }
    if (handled) continue;

    std::string arg;
    if (auto eq = flag.find('='); eq != std::string::npos) arg = flag.substr(eq + 1);

    if (StartsWith(flag, "encryptable=")) {
      // Adoptable-storage marker; the argument is ignored.
      entry->fs_mgr_flags.crypt = true;
    } else if (StartsWith(flag, "voldmanaged=")) {
      entry->fs_mgr_flags.vold_managed = true;
      if (auto parts = Split(arg, ":"); parts.size() == 2) {
        entry->label = std::move(parts[0]);
        if (parts[1] != "auto" && !ParseInt(parts[1], &entry->partnum)) {
          entry->partnum = -1;
          LOG(WARNING) << "fstab:" << line_no << ": malformed voldmanaged=: " << arg;
        }
      } else {
        LOG(WARNING) << "fstab:" << line_no << ": malformed voldmanaged=: " << arg;
      }
    } else if (StartsWith(flag, "length=")) {
      if (!ParseInt(arg, &entry->length)) {
        LOG(WARNING) << "fstab:" << line_no << ": malformed length=: " << arg;
      }
    } else if (StartsWith(flag, "swapprio=")) {
      if (!ParseInt(arg, &entry->swap_prio)) {
        LOG(WARNING) << "fstab:" << line_no << ": malformed swapprio=: " << arg;
      }
    } else if (StartsWith(flag, "zramsize=")) {
      if (!arg.empty() && arg.back() == '%') {
        int percent;
        if (ParseInt(arg.substr(0, arg.size() - 1), &percent, 0, 200)) {
          entry->zram_size = CalculateZramSize(percent);
        } else {
          LOG(WARNING) << "fstab:" << line_no << ": malformed zramsize=: " << arg;
        }
      } else if (!ParseInt(arg, &entry->zram_size)) {
        LOG(WARNING) << "fstab:" << line_no << ": malformed zramsize=: " << arg;
      }
    } else if (StartsWith(flag, "fileencryption=") || flag == "fileencryption") {
      entry->fs_mgr_flags.file_encryption = true;
      entry->encryption_options = arg;
    } else if (StartsWith(flag, "max_comp_streams=")) {
      if (!ParseInt(arg, &entry->max_comp_streams)) {
        LOG(WARNING) << "fstab:" << line_no << ": malformed max_comp_streams=: " << arg;
      }
    } else if (StartsWith(flag, "reservedsize=")) {
      uint64_t size;
      if (ParseByteCount(arg, &size)) {
        entry->reserved_size = static_cast<int64_t>(size);
      } else {
        LOG(WARNING) << "fstab:" << line_no << ": malformed reservedsize=: " << arg;
      }
    } else if (StartsWith(flag, "readahead_size_kb=")) {
      int kb;
      if (ParseInt(arg, &kb, 0, 16 * 1024)) {
        entry->readahead_size_kb = kb;
      } else {
        LOG(WARNING) << "fstab:" << line_no
            << ": malformed readahead_size_kb= (0 ~ 16M): " << arg;
      }
    } else if (StartsWith(flag, "eraseblk=")) {
      int64_t val;
      if (!ParseInt(arg, &val) || val < 4096 || (val & (val - 1)) != 0) {
        LOG(WARNING) << "fstab:" << line_no << ": malformed eraseblk=: " << arg;
      } else {
        entry->erase_blk_size = val;
      }
    } else if (StartsWith(flag, "logicalblk=")) {
      int64_t val;
      if (!ParseInt(arg, &val) || val < 4096 || (val & (val - 1)) != 0) {
        LOG(WARNING) << "fstab:" << line_no << ": malformed logicalblk=: " << arg;
      } else {
        entry->logical_blk_size = val;
      }
    } else if (StartsWith(flag, "avb_keys=")) {
      // Must precede the bare "avb" prefix match.
      entry->avb_keys = arg;
    } else if (StartsWith(flag, "avb_hashtree_digest=")) {
      entry->avb_hashtree_digest = arg;
    } else if (StartsWith(flag, "avb")) {
      entry->fs_mgr_flags.avb = true;
      entry->vbmeta_partition = arg;
    } else if (StartsWith(flag, "keydirectory=")) {
      entry->metadata_key_dir = arg;
    } else if (StartsWith(flag, "metadata_encryption=")) {
      entry->metadata_encryption_options = arg;
    } else if (StartsWith(flag, "sysfs_path=")) {
      entry->sysfs_path = arg;
    } else if (StartsWith(flag, "zram_backingdev_size=")) {
      if (!ParseByteCount(arg, &entry->zram_backingdev_size)) {
        LOG(WARNING) << "fstab:" << line_no
            << ": malformed zram_backingdev_size=: " << arg;
      }
    } else {
      LOG(WARNING) << "fstab:" << line_no << ": unknown flag: " << flag;
      entry->unknown_flags.push_back(flag);
    }
  }
}

std::string StripQuotes(std::string value) {
  std::erase(value, '"');
  return value;
}

// Splits on any delimiter character, but never inside double quotes: twrp.flags
// values like display="USB Storage" or fsflags="ro,..." contain spaces and
// commas. Keeps empty fields; callers drop what they do not want.
std::vector<std::string> SplitRespectingQuotes(const std::string& s, const char* delims) {
  std::vector<std::string> result;
  std::string current;
  bool in_quotes = false;
  for (char c : s) {
    if (c == '"') {
      in_quotes = !in_quotes;
      current.push_back(c);
    } else if (!in_quotes && strchr(delims, c) != nullptr) {
      result.push_back(std::move(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  result.push_back(std::move(current));
  return result;
}

// Classic-format boolean: a bare flag means true, "1" true, "0" false.
bool TwrpBoolValue(const std::string& value, bool* out) {
  if (value.empty() || value == "1") {
    *out = true;
    return true;
  }
  if (value == "0") {
    *out = false;
    return true;
  }
  return false;
}

// Parses the semicolon-separated token list of a twrp.flags flags= column.
void ParseTwrpFlagList(const std::string& flags, size_t line_no, FstabEntry* entry) {
  for (const auto& flag : SplitRespectingQuotes(flags, ";")) {
    if (flag.empty()) continue;

    std::string key = flag;
    std::string value;
    if (auto eq = flag.find('='); eq != std::string::npos) {
      key = flag.substr(0, eq);
      value = StripQuotes(flag.substr(eq + 1));
    }

    bool handled = false;
    for (const auto& [name, field] : kTwrpBoolFlags) {
      if (key == name) {
        bool val;
        if (TwrpBoolValue(value, &val)) {
          entry->twrp_flags.*field = val;
        } else {
          LOG(WARNING) << "twrp.flags:" << line_no << ": malformed " << key << "=: " << value;
        }
        handled = true;
        break;
      }
    }
    if (handled) continue;

    if (key == "slotselect") {
      bool val;
      if (TwrpBoolValue(value, &val)) {
        entry->fs_mgr_flags.slot_select = val;
      } else {
        LOG(WARNING) << "twrp.flags:" << line_no << ": malformed slotselect=: " << value;
      }
    } else if (key == "display") {
      entry->twrp_flags.display = value;
    } else if (key == "backup") {
      // Bare "backup" enables it; otherwise take the policy value verbatim.
      if (value.empty()) {
        entry->twrp_flags.backup = 1;
      } else if (!ParseInt(value, &entry->twrp_flags.backup)) {
        LOG(WARNING) << "twrp.flags:" << line_no << ": malformed backup=: " << value;
      }
    } else if (key == "backupname") {
      entry->twrp_flags.backupname = value;
    } else if (key == "subpartitionof") {
      entry->twrp_flags.subpartitionof = value;
    } else if (key == "fsflags") {
      entry->twrp_flags.fsflags = value;
    } else if (key == "length") {
      // The classic format's length= maps to the fstab2 field.
      if (!ParseInt(value, &entry->length)) {
        LOG(WARNING) << "twrp.flags:" << line_no << ": malformed length=: " << value;
      }
    } else {
      LOG(WARNING) << "twrp.flags:" << line_no << ": unknown flag: " << flag;
      entry->unknown_flags.push_back(flag);
    }
  }
}

// Reads androidboot.<key> from /proc/bootconfig, then /proc/cmdline.
std::string GetBootConfig(const std::string& key) {
  const std::string full_key = "androidboot." + key;

  std::string bootconfig;
  if (ReadFileToString("/proc/bootconfig", &bootconfig, /*follow_symlinks=*/true)) {
    // One "key = value" pair per line.
    for (const auto& line : Split(bootconfig, "\n")) {
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      if (Trim(line.substr(0, eq)) != full_key) continue;
      return StripQuotes(Trim(line.substr(eq + 1)));
    }
  }

  std::string cmdline;
  if (ReadFileToString("/proc/cmdline", &cmdline, /*follow_symlinks=*/true)) {
    for (const auto& token : Tokenize(cmdline, " \t\n")) {
      auto eq = token.find('=');
      if (eq == std::string::npos) continue;
      if (token.substr(0, eq) != full_key) continue;
      return StripQuotes(token.substr(eq + 1));
    }
  }

  return "";
}

}  // namespace

FstabParseResult ParseFstabFromString(const std::string& contents) {
  FstabParseResult result;

  const auto lines = Split(contents, "\n");
  for (size_t line_no = 0; line_no < lines.size(); ++line_no) {
    std::string line = lines[line_no];
    // Tolerate DOS line endings; some device fstabs ship them.
    if (!line.empty() && line.back() == '\r') line.pop_back();

    auto fields = Tokenize(line, " \t");
    if (fields.empty() || StartsWith(fields.front(), "#")) continue;

    if (fields.size() < 5) {
      LOG(WARNING) << "fstab:" << line_no + 1 << ": expected 5 fields, got "
          << fields.size() << ", skipping: " << line;
      ++result.skipped_lines;
      continue;
    }
    if (fields.size() > 5) {
      LOG(WARNING) << "fstab:" << line_no + 1 << ": ignoring extra columns: " << line;
    }

    FstabEntry entry;
    entry.blk_device = std::move(fields[0]);
    entry.mount_point = std::move(fields[1]);
    entry.fs_type = std::move(fields[2]);
    ParseMountFlags(fields[3], &entry);
    ParseFsMgrFlags(fields[4], line_no + 1, &entry);
    entry.raw_mnt_flags = std::move(fields[3]);
    entry.raw_fsmgr_flags = std::move(fields[4]);
    if (entry.fs_mgr_flags.logical) {
      entry.logical_partition_name = entry.blk_device;
    }

    result.unknown_flags += entry.unknown_flags.size();
    result.fstab.push_back(std::move(entry));
  }

  return result;
}

FstabParseResult ParseFstabFromFile(const std::string& path) {
  std::string contents;
  // Follow symlinks; /etc/recovery.fstab may be one on some devices.
  if (!ReadFileToString(path, &contents, /*follow_symlinks=*/true)) {
    PLOG(WARNING) << "fstab: failed to read " << path;
    return {};
  }
  return ParseFstabFromString(contents);
}

FstabParseResult ParseTwrpFlagsFromString(const std::string& contents) {
  FstabParseResult result;

  const auto lines = Split(contents, "\n");
  for (size_t line_no = 0; line_no < lines.size(); ++line_no) {
    std::string line = lines[line_no];
    if (!line.empty() && line.back() == '\r') line.pop_back();

    // Quote-aware split: values like display="USB Storage" contain spaces.
    auto fields = SplitRespectingQuotes(line, " \t");
    std::erase_if(fields, [](const std::string& f) { return f.empty(); });
    if (fields.empty() || StartsWith(fields.front(), "#")) continue;

    // The trailing flags= column is optional, like device2 before it.
    std::string flags_field;
    if (StartsWith(fields.back(), "flags=")) {
      flags_field = fields.back().substr(strlen("flags="));
      fields.pop_back();
    }

    if (fields.size() < 3) {
      LOG(WARNING) << "twrp.flags:" << line_no + 1 << ": expected at least 3 fields, got "
          << fields.size() << ", skipping: " << line;
      ++result.skipped_lines;
      continue;
    }

    FstabEntry entry;
    entry.mount_point = std::move(fields[0]);
    entry.fs_type = std::move(fields[1]);
    entry.blk_device = std::move(fields[2]);
    // Trailing columns: alternate devices, a column-form length= and NULL
    // placeholders, mirroring the classic line grammar; the last '/'-prefixed
    // column wins, like the legacy tokenizer.
    for (size_t i = 3; i < fields.size(); ++i) {
      const std::string& tok = fields[i];
      if (StartsWith(tok, "/")) {
        entry.blk_device2 = tok;
      } else if (StartsWith(tok, "length=")) {
        if (!ParseInt(tok.substr(strlen("length=")), &entry.length)) {
          LOG(WARNING) << "twrp.flags:" << line_no + 1 << ": malformed length=: " << tok;
        }
      } else if (tok != "NULL" && tok != "null") {
        LOG(WARNING) << "twrp.flags:" << line_no + 1 << ": unknown column: " << tok;
      }
    }
    entry.twrp_flags.raw_flags = flags_field;
    ParseTwrpFlagList(flags_field, line_no + 1, &entry);

    result.unknown_flags += entry.unknown_flags.size();
    result.fstab.push_back(std::move(entry));
  }

  return result;
}

FstabParseResult ParseTwrpFlagsFromFile(const std::string& path) {
  std::string contents;
  if (!ReadFileToString(path, &contents, /*follow_symlinks=*/true)) {
    PLOG(WARNING) << "twrp.flags: failed to read " << path;
    return {};
  }
  return ParseTwrpFlagsFromString(contents);
}

void ApplyTwrpFlags(Fstab* fstab, const Fstab& twrp_entries) {
  for (const auto& twrp : twrp_entries) {
    bool merged = false;
    for (auto& entry : *fstab) {
      if (entry.mount_point != twrp.mount_point) continue;
      entry.twrp_flags = twrp.twrp_flags;
      entry.blk_device2 = twrp.blk_device2;
      entry.fs_mgr_flags.slot_select |= twrp.fs_mgr_flags.slot_select;
      merged = true;
    }
    if (!merged) fstab->push_back(twrp);
  }
}

std::vector<FstabEntry*> GetEntriesForMountPoint(Fstab* fstab, const std::string& mount_point) {
  std::vector<FstabEntry*> entries;
  for (auto& entry : *fstab) {
    if (entry.mount_point == mount_point) entries.push_back(&entry);
  }
  return entries;
}

std::vector<const FstabEntry*> GetEntriesForMountPoint(const Fstab* fstab,
                                                       const std::string& mount_point) {
  std::vector<const FstabEntry*> entries;
  for (const auto& entry : *fstab) {
    if (entry.mount_point == mount_point) entries.push_back(&entry);
  }
  return entries;
}

FstabEntry* GetEntryForMountPoint(Fstab* fstab, const std::string& mount_point) {
  auto entries = GetEntriesForMountPoint(fstab, mount_point);
  return entries.empty() ? nullptr : entries.front();
}

const FstabEntry* GetEntryForMountPoint(const Fstab* fstab, const std::string& mount_point) {
  auto entries = GetEntriesForMountPoint(fstab, mount_point);
  return entries.empty() ? nullptr : entries.front();
}

FstabEntry* GetEntryForMountPoint(Fstab* fstab, const std::string& mount_point,
                                  const std::string& fstype) {
  for (auto& entry : *fstab) {
    if (entry.mount_point == mount_point && entry.fs_type == fstype) return &entry;
  }
  return nullptr;
}

const FstabEntry* GetEntryForMountPoint(const Fstab* fstab, const std::string& mount_point,
                                        const std::string& fstype) {
  for (const auto& entry : *fstab) {
    if (entry.mount_point == mount_point && entry.fs_type == fstype) return &entry;
  }
  return nullptr;
}

std::string GetRecoveryFstabPath() {
  for (const char* key : {"fstab_suffix", "hardware", "hardware.platform"}) {
    const std::string suffix = GetBootConfig(key);
    if (suffix.empty()) continue;

    std::string path = "/etc/recovery.fstab." + suffix;
    if (access(path.c_str(), F_OK) == 0) return path;
  }
  return "/etc/recovery.fstab";
}

std::string GetSlotSuffix() {
  return GetBootConfig("slot_suffix");
}

std::string OtherSlotSuffix(const std::string& slot_suffix) {
  if (slot_suffix == "_a") return "_b";
  if (slot_suffix == "_b") return "_a";
  return "";
}

bool ApplySlotSuffixes(Fstab* fstab) {
  std::string suffix;
  for (auto& entry : *fstab) {
    if (!entry.fs_mgr_flags.slot_select && !entry.fs_mgr_flags.slot_select_other) continue;

    if (suffix.empty()) {
      suffix = GetSlotSuffix();
      if (suffix.empty()) return false;
    }

    const std::string update =
        entry.fs_mgr_flags.slot_select ? suffix : OtherSlotSuffix(suffix);
    entry.blk_device += update;
    entry.logical_partition_name += update;
  }
  return true;
}

}