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

// Parser for Android fstab files ("fstab2" format, the layout used by
// recovery.fstab and vendor fstab.<suffix> files):
//
//   <blk_device> <mount_point> <fs_type> <mnt_flags> <fsmgr_flags>
//
// Field semantics follow AOSP system/core/fs_mgr/libfstab, with
// recovery-oriented deviations:
//   - unknown fsmgr_flags tokens are kept verbatim in
//     FstabEntry::unknown_flags instead of being dropped
//   - a malformed line is skipped with a warning instead of failing the file
//   - slot suffixes are never applied during parsing; ApplySlotSuffixes() is
//     an explicit, opt-in step
//
// Also parses the classic TWRP format of twrp.flags files:
//
//   <mount_point> <fs_type> <blk_device> [<blk_device2>] [length=<n>] [flags=<f1;f2;...>]
//
// whose semicolon-separated tokens populate FstabEntry::twrp_flags.

#pragma once

#include <sys/mount.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace twrp {
namespace fstab {

struct FsMgrFlags {
    bool wait = false;
    bool check = false;
    bool nonremovable = false;
    bool recovery_only = false;
    bool vold_managed = false;
    bool no_emulated_sd = false;
    bool no_trim = false;
    bool formattable = false;
    bool slot_select = false;
    bool slot_select_other = false;
    bool late_mount = false;
    bool no_fail = false;
    bool quota = false;
    bool avb = false;
    bool logical = false;
    bool checkpoint_blk = false;
    bool checkpoint_fs = false;
    bool first_stage_mount = false;
    bool fs_verity = false;
    bool ext_meta_csum = false;
    bool fs_compress = false;
    bool overlayfs_remove_missing_lowerdir = false;
    bool wrapped_key = false;
    bool overlay_on = false;
    bool overlay_off = false;
    bool file_encryption = false;
    // "encryptable=" now only marks adoptable-storage volumes (FDE is gone);
    // "resize" was dropped by AOSP but is still common in device fstabs.
    bool crypt = false;
    bool resize = false;
};

// Flags from the flags=... column of twrp.flags (classic TWRP format).
struct TwrpFlags {
    // The flags= token list verbatim (order and quoting intact), for
    // consumers that interpret the flags themselves.
    std::string raw_flags;

    // display=: GUI name; empty means derive one from the mount point.
    std::string display;
    // backup=: backup policy value, -1 when unspecified.
    int backup = -1;
    // backupname=: custom name inside backups.
    std::string backupname;
    // subpartitionof=: mount point this partition is grouped under.
    std::string subpartitionof;
    // fsflags=: mount options replacing fs_options.
    std::string fsflags;
    bool flashimg = false;
    bool mounttodecrypt = false;
    bool storage = false;
    bool wipeingui = false;
    bool removable = false;
    bool canbeencrypted = false;
    bool canencryptbackup = false;
    bool usermrf = false;
};

struct FstabEntry {
    std::string blk_device;
    // Alternate device from twrp.flags (whole-disk fallback for removable media).
    std::string blk_device2;
    // Equals blk_device for logical (dynamic partition) entries.
    std::string logical_partition_name;
    std::string mount_point;
    std::string fs_type;

    // MS_* mount flags from the mnt_flags column.
    unsigned long flags = 0;
    // Non-MS_* options from the mnt_flags column, comma-joined.
    std::string fs_options;
    // The mnt_flags and fsmgr_flags columns verbatim, for consumers that
    // interpret the columns themselves.
    std::string raw_mnt_flags;
    std::string raw_fsmgr_flags;

    FsMgrFlags fs_mgr_flags;

    TwrpFlags twrp_flags;

    // Flags that carry an argument.
    int64_t length = 0;
    // voldmanaged= label.
    std::string label;
    // voldmanaged= partition number, -1 for "auto".
    int partnum = -1;
    int swap_prio = -1;
    int max_comp_streams = 0;
    // zramsize= in bytes; a percentage of RAM is expanded.
    int64_t zram_size = 0;
    // reservedsize= and f2fs reserve_root=.
    int64_t reserved_size = 0;
    int readahead_size_kb = -1;
    int64_t erase_blk_size = 0;
    int64_t logical_blk_size = 0;
    uint64_t zram_backingdev_size = 0;
    // fileencryption= options.
    std::string encryption_options;
    // keydirectory=.
    std::string metadata_key_dir;
    // metadata_encryption= options.
    std::string metadata_encryption_options;
    // sysfs_path=.
    std::string sysfs_path;
    // avb= argument (empty for a bare "avb").
    std::string vbmeta_partition;
    std::string avb_keys;
    std::string avb_hashtree_digest;
    // lowerdir= from the mnt_flags column (overlayfs).
    std::string lowerdir;

    // fsmgr_flags tokens not understood by this parser, verbatim and in
    // source order.
    std::vector<std::string> unknown_flags;
};

// Entries stay in file order; lookups return the first match, like AOSP.
using Fstab = std::vector<FstabEntry>;

struct FstabParseResult {
    Fstab fstab;
    // Malformed lines dropped with a warning.
    size_t skipped_lines = 0;
    // Tokens kept in FstabEntry::unknown_flags.
    size_t unknown_flags = 0;

    bool Ok() const { return !fstab.empty(); }
};

// Parses fstab text. Content problems are reported per line as warnings and
// counted in the result; only a completely empty result means failure.
FstabParseResult ParseFstabFromString(const std::string& contents);

// Convenience wrapper; a missing or unreadable file yields an empty result.
FstabParseResult ParseFstabFromFile(const std::string& path);

// Parses classic TWRP format text (twrp.flags). Lines are
//   <mount_point> <fs_type> <blk_device> [<blk_device2>] [length=<n>] [flags=<f1;f2;...>]
// with the same lenient policy as ParseFstabFromString(); "slotselect" is
// routed to fs_mgr_flags, everything else populates twrp_flags.
FstabParseResult ParseTwrpFlagsFromString(const std::string& contents);

// Convenience wrapper; a missing or unreadable file yields an empty result.
FstabParseResult ParseTwrpFlagsFromFile(const std::string& path);

// Merges parsed twrp.flags entries into a fstab. Entries whose mount point
// already exists only get twrp_flags (and blk_device2, slotselect) filled in;
// the fstab stays authoritative for device, fstype and mount flags. Entries
// with no fstab counterpart are appended as new partitions.
void ApplyTwrpFlags(Fstab* fstab, const Fstab& twrp_entries);

std::vector<FstabEntry*> GetEntriesForMountPoint(Fstab* fstab, const std::string& mount_point);
std::vector<const FstabEntry*> GetEntriesForMountPoint(const Fstab* fstab,
                                                       const std::string& mount_point);
FstabEntry* GetEntryForMountPoint(Fstab* fstab, const std::string& mount_point);
const FstabEntry* GetEntryForMountPoint(const Fstab* fstab, const std::string& mount_point);
FstabEntry* GetEntryForMountPoint(Fstab* fstab, const std::string& mount_point,
                                  const std::string& fstype);
const FstabEntry* GetEntryForMountPoint(const Fstab* fstab, const std::string& mount_point,
                                         const std::string& fstype);

// AOSP recovery fstab discovery: the first existing of
// /etc/recovery.fstab.<fstab_suffix|hardware|hardware.platform>, else
// /etc/recovery.fstab.
std::string GetRecoveryFstabPath();

// Slot suffix from androidboot.slot_suffix (bootconfig, then kernel cmdline);
// empty when the device has no slots.
std::string GetSlotSuffix();
std::string OtherSlotSuffix(const std::string& slot_suffix);

// Appends _a/_b to slotselect / slotselect_other entries, like AOSP
// fs_mgr_update_for_slotselect. Returns false if such an entry exists but
// GetSlotSuffix() is empty.
bool ApplySlotSuffixes(Fstab* fstab);

}  // namespace fstab
}  // namespace twrp
