#include "twrp_tools_backend.h"

#include <unistd.h>

#include <fstream>

#include "data.hpp"
#include "gui2/console/stdio_adapter.h"
#include "gui2/console/messages.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp_functions.hpp"
#include "twrpRepacker.hpp"
#include "twrpinstall/include/set_metadata.h"
#include "twrp_operation.h"
#include "unit_conversion.hpp"
#include "variables.h"

namespace gui2_backend {

namespace {

bool flag(const char* name) {
  return DataManager::GetIntValue(name) != 0;
}

int repair(const std::string& part_path) {
  int op_status = 0;

  operation_start("Repair Partition");
  if (PartitionManager.RepairByPath(part_path, true)) {
    op_status = 0;  // success
  } else {
    op_status = 1;  // fail
  }

  operation_end(op_status);
  return op_status;
}

int resize(const std::string& part_path) {
  int op_status = 0;

  operation_start("Resize Partition");
  if (PartitionManager.ResizeByPath(part_path, true)) {
    op_status = 0;  // success
  } else {
    op_status = 1;  // fail
  }

  operation_end(op_status);
  return op_status;
}

int changefilesystem(const std::string& part_path, const std::string& file_system) {
  int op_status = 0;

  operation_start("Change File System");
  if (PartitionManager.WipeByPath(part_path, file_system)) {
    op_status = 0;  // success
  } else {
    gui2_console::log_error("change_fs_err");
    op_status = 1;  // fail
  }
  PartitionManager.UpdateSystemDetails();
  operation_end(op_status);
  return op_status;
}

// GUIAction::cmd
int cmd(std::string arg) {
  int op_status = 0;

  operation_start("Command");
  LOGINFO("Running command: '%s'\n", arg.c_str());
  op_status = TWFunc::ExecCmd(arg);
  if (op_status != 0) op_status = 1;

  operation_end(op_status);
  return op_status;
}

// GUIAction::copylog
int copylog() {
  operation_start("Copy Log");
  {
    std::string dst, curr_storage;
    int copy_kernel_log = 0;
    int copy_logcat = 1;

    DataManager::GetValue("tw_include_kernel_log", copy_kernel_log);
    DataManager::GetValue("tw_include_logcat", copy_logcat);
    PartitionManager.MountCurrentStorage(true);
    curr_storage = DataManager::GetCurrentStoragePath();
    dst = curr_storage + "/recovery.log";
    TWFunc::CopyFile("/tmp/recovery.log", dst.c_str(), 0755);
    tw_set_default_metadata(dst.c_str());
    if (copy_kernel_log) TWFunc::CopyKernelLog(curr_storage);
    if (copy_logcat) TWFunc::CopyLogcat(curr_storage);
    sync();
    gui2_console::info("copy_log", dst);
  }
  operation_end(0);
  return 0;
}

// GUIAction::fixcontexts
int fixcontexts() {
  int op_status = 0;

  operation_start("Fix Contexts");
  LOGINFO("fix contexts started!\n");
  op_status = PartitionManager.FixContexts();
  if (op_status != 0) op_status = 1;  // failure
  operation_end(op_status);
  return op_status;
}

// GUIAction::unmapsuperdevices
int unmapsuperdevices() {
  int op_status = 1;

  operation_start("Remove Super Devices");
  if (PartitionManager.UnmapSuperDevices()) {
    op_status = 0;
  }

  operation_end(op_status);
  return op_status;
}

// GUIAction::reflashtwrp
int reflashtwrp() {
  int op_status = 1;
  twrpRepacker repacker;

  operation_start("Repack Image");
  if (!repacker.Flash_Current_Twrp()) goto exit;
  op_status = 0;
exit:
  operation_end(op_status);
  return op_status;
}

// GUIAction::repackimage
int repackimage() {
  int op_status = 1;
  twrpRepacker repacker;

  operation_start("Repack Image");
  {
    std::string path = DataManager::GetStrValue("tw_filename");
    RepackOptionsStruct Repack_Options;
    Repack_Options.Disable_Verity = false;
    Repack_Options.Disable_Force_Encrypt = false;
    Repack_Options.Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0;
    if (DataManager::GetIntValue("tw_repack_kernel") == 1)
      Repack_Options.Type = REPLACE_KERNEL;
    else
      Repack_Options.Type = REPLACE_RAMDISK;
    if (!repacker.Repack_Image_And_Flash(path, Repack_Options)) goto exit;
  }
  op_status = 0;
exit:
  operation_end(op_status);
  return op_status;
}

// GUIAction::refreshsizes
int refreshsizes() {
  operation_start("Refreshing Sizes");
  PartitionManager.UpdateSystemDetails();
  operation_end(0);
  return 0;
}

// GUIAction::applycustomtwrpfolder
int applycustomtwrpfolder(std::string arg) {
  operation_start("ChangingTWRPFolder");
  std::string storageFolder = DataManager::GetCurrentStoragePath();
  std::string newFolder = storageFolder + '/' + arg;
  std::string newBackupFolder = newFolder + "/BACKUPS/" + DataManager::GetStrValue("device_id");
  std::string prevFolder = storageFolder + DataManager::GetStrValue(TW_RECOVERY_FOLDER_VAR);
  bool ret = false;

  if (TWFunc::IsPathExists(newFolder)) {
    gui2_console::log_error("tw_folder_exists");
  } else {
    ret = true;
  }

  if (newFolder != prevFolder && ret) {
    // Nothing has been put under the old name until a backup or a theme
    // lands there, so there may be nothing to move.
    if (TWFunc::IsPathExists(prevFolder))
      ret = TWFunc::ExecCmd("mv -f \"" + prevFolder + "\" \"" + newFolder + '\"') == 0;
  } else {
    gui2_console::log_error("tw_folder_exists");
  }

  if (ret) ret = TWFunc::RecursiveMkdir(newBackupFolder) ? true : false;

  if (ret) {
    DataManager::SetValue(TW_RECOVERY_FOLDER_VAR, '/' + arg);
    DataManager::SetValue(TW_BACKUPS_FOLDER_VAR, newBackupFolder);
    // Creates an empty file that marks which folder is TWRP with the renamed new name, after reboot.
    std::string path = newFolder + "/.twrpcf";
    std::ofstream twrpcf(path);
    twrpcf.close();
  }
  operation_end((int)!ret);
  return (int)!ret;
}

// GUIAction::fixabrecoverybootloop
int fixabrecoverybootloop() {
  int op_status = 1;
  twrpRepacker repacker;

  operation_start("Repack Image");
  {
    if (!TWFunc::IsPathExists("/system/bin/magiskboot")) {
      LOGERR("Image repacking tool not present in this TWRP build!");
      goto exit;
    }
    DataManager::SetProgress(0);
    TWPartition* part = PartitionManager.FindPartitionByPath("/boot");
    if (part)
      gui2_console::info("unpacking_image", part->GetDisplayName());
    else {
      gui2_console::log_error("unable_to_locate", "/boot");
      goto exit;
    }
    if (!repacker.Backup_Image_For_Repack(part, REPACK_ORIG_DIR,
                                          DataManager::GetIntValue("tw_repack_backup_first") != 0,
                                          gui_lookup("repack", "Repack")))
      goto exit;
    DataManager::SetProgress(.25);
    gui2_console::info("fixing_recovery_loop_patch");
    std::string command = "cd " REPACK_ORIG_DIR
                          " && /system/bin/magiskboot hexpatch kernel "
                          "77616E745F696E697472616D667300 736B69705F696E697472616D667300";
    if (TWFunc::ExecCmd(command) != 0) {
      gui2_console::log_error("fix_recovery_loop_patch_error");
      goto exit;
    }
    std::string header_path = REPACK_ORIG_DIR;
    header_path += "header";
    if (TWFunc::IsPathExists(header_path)) {
      command = "cd " REPACK_ORIG_DIR
                " && sed -i \"s|$(grep '^cmdline=' header | cut -d= -f2-)|$(grep '^cmdline=' "
                "header | cut -d= -f2- | sed -e 's/skip_override//' -e 's/  */ /g' -e "
                "'s/[ \t]*$//')|\" header";
      if (TWFunc::ExecCmd(command) != 0) {
        gui2_console::log_error("fix_recovery_loop_patch_error");
        goto exit;
      }
    }
    DataManager::SetProgress(.5);
    gui2_console::info("repacking_image", part->GetDisplayName());
    command = "cd " REPACK_ORIG_DIR " && /system/bin/magiskboot repack " REPACK_ORIG_DIR "boot.img";
    if (TWFunc::ExecCmd(command) != 0) {
      gui2_console::log_error("repack_error");
      goto exit;
    }
    DataManager::SetProgress(.75);
    std::string path = REPACK_ORIG_DIR;
    std::string file = "new-boot.img";
    DataManager::SetValue("tw_flash_partition", "/boot;");
    if (!PartitionManager.FlashImage(path, file)) {
      LOGINFO("Error flashing new image\n");
      goto exit;
    }
    DataManager::SetProgress(1);
    TWFunc::RemoveDir(REPACK_ORIG_DIR, false);
  }
  op_status = 0;
exit:
  operation_end(op_status);
  return op_status;
}

// GUIAction::mergesnapshots, which has no operation_start of its own.
int mergesnapshots() {
  int op_status = 1;
  if (PartitionManager.CheckPendingMerges()) {
    op_status = 0;
  }
  operation_end(op_status);
  return op_status;
}

// GUIAction::disableAVB2
int disableAVB2() {
  int op_status = 1;
  operation_start("Disable AVB2.0");
  gui2_console::highlight("disabling_AVB2");
  if (PartitionManager.DisableAvb2(true)) {
    op_status = 0;
  }
  operation_end(op_status);
  return op_status;
}

std::string base_name(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

twrp_tools_backend::~twrp_tools_backend() {
  if (worker_.joinable()) worker_.join();
}

bool twrp_tools_backend::details(const std::string& mount_point, partition_details* out) {
  if (out == nullptr) return false;
  *out = {};
  TWPartition* part = PartitionManager.FindPartitionByPath(mount_point);
  if (part == nullptr) {
    LOGERR("Unable to locate partition: '%s'\n", mount_point.c_str());
    return false;
  }

  out->name = part->display_name_;
  out->mount_point = part->mount_point_;
  out->file_system = part->current_file_system_;
  out->size = UnitConversion::FormatBytes(part->size_);
  out->used = UnitConversion::FormatBytes(part->used_);
  out->free = UnitConversion::FormatBytes(part->free_);
  out->backup_size = UnitConversion::FormatBytes(part->backup_size_);
  out->present = part->is_present_;
  out->removable = part->removable_;
  out->can_repair = part->CanRepair();
  out->can_resize = part->CanResize();
  if (TWFunc::IsPathExists("/system/bin/mke2fs"))
    out->file_systems.insert(out->file_systems.end(), { "ext2", "ext3", "ext4" });
  if (TWFunc::IsPathExists("/system/bin/mkfs.fat")) out->file_systems.push_back("vfat");
  if (TWFunc::IsPathExists("/system/bin/mkfs.exfat")) out->file_systems.push_back("exfat");
  if (TWFunc::IsPathExists("/system/bin/make_f2fs")) out->file_systems.push_back("f2fs");
  return true;
}

// The conditions on the legacy advanced page's buttons.
tool_availability twrp_tools_backend::availability() {
  tool_availability result;
  result.twrp_folder = flag(TW_IS_DECRYPTED);
  result.fix_recovery_bootloop =
      flag("tw_has_boot_slots") && flag("tw_has_repack_tools") && flag("tw_uses_initramfs");
  result.merge_snapshots = flag(TW_VIRTUAL_AB_ENABLED);
  result.disable_avb2 = true;
  result.fix_contexts = flag(TW_HAS_DATA_MEDIA);
  const bool repack = flag("tw_has_boot_slots") && flag("tw_has_repack_tools");
  result.install_ramdisk = repack && flag("tw_include_install_recovery_ramdisk");
  result.reflash_twrp =
      repack && !flag("tw_no_flash_current_twrp") && !flag("tw_is_vendor_boot_header_v3");
  result.install_kernel = repack;
  result.unmap_super_devices = flag("tw_is_super");
  result.sideload = flag(TW_HAS_DATA_MEDIA);
  return result;
}

std::string twrp_tools_backend::twrp_folder() {
  return DataManager::GetStrValue(TW_RECOVERY_FOLDER_VAR);
}

std::string twrp_tools_backend::storage_path() {
  return DataManager::GetCurrentStoragePath();
}

bool twrp_tools_backend::path_exists(const std::string& path) {
  return TWFunc::IsPathExists(path);
}

bool twrp_tools_backend::users_locked() {
  return flag("tw_is_fbe") && !flag("tw_all_users_decrypted");
}

void twrp_tools_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_tools_backend::start(tool_job job, const std::string& target, const std::string& value) {
  if (running_.load()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = tool_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_tools_backend::run, this, job, target, value);
  return true;
}

void twrp_tools_backend::run(tool_job job, std::string target, std::string value) {
  const std::string backups = DataManager::GetStrValue(TW_BACKUPS_FOLDER_VAR);
  const std::string name = base_name(target);
  int op_status = 1;
  switch (job) {
    case tool_job::REPAIR:
      op_status = repair(target);
      break;
    case tool_job::RESIZE:
      op_status = resize(target);
      break;
    case tool_job::CHANGE_FILE_SYSTEM:
      op_status = changefilesystem(target, value);
      break;
    case tool_job::RENAME_BACKUP:
      op_status = cmd("cd " + backups + " && mv \"" + name + "\" \"" +
                      value + "\"");
      break;
    case tool_job::DELETE_BACKUP:
      op_status = cmd("cd " + backups + " && rm -rf \"" + name + "\"");
      break;
    case tool_job::TWRP_FOLDER:
      op_status = applycustomtwrpfolder(value);
      break;
    case tool_job::FIX_RECOVERY_BOOTLOOP:
      op_status = fixabrecoverybootloop();
      break;
    case tool_job::MERGE_SNAPSHOTS:
      op_status = mergesnapshots();
      break;
    case tool_job::DISABLE_AVB2:
      op_status = disableAVB2();
      break;
    case tool_job::REFRESH_SIZES:
      op_status = refreshsizes();
      break;
    case tool_job::COMMAND:
      op_status = cmd(value);
      break;
    case tool_job::COPY_LOG:
      op_status = copylog();
      break;
    case tool_job::FIX_CONTEXTS:
      op_status = fixcontexts();
      break;
    case tool_job::UNMAP_SUPER_DEVICES:
      op_status = unmapsuperdevices();
      break;
    case tool_job::REFLASH_TWRP:
      op_status = reflashtwrp();
      break;
    case tool_job::REPACK_IMAGE:
      op_status = repackimage();
      break;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = op_status == 0 ? tool_state::DONE : tool_state::FAILED;
  }
  running_.store(false);
}

tool_state twrp_tools_backend::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void twrp_tools_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != tool_state::RUNNING) state_ = tool_state::IDLE;
}

}  // namespace gui2_backend
