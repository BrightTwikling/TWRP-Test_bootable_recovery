#include "twrp_install_backend.h"

#include <sys/mount.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <cutils/properties.h>

#include "data.hpp"
#include "gui2/console/stdio_adapter.h"
#include "gui2/console/messages.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twinstall.h"
#include "twrp_functions.hpp"
#include "twrp_operation.h"
#include "twrp_operation_executor.h"
#include "variables.h"

namespace gui2_backend {
namespace {

#ifdef TW_OZIP_DECRYPT_KEY
int ozip_decrypt(std::string zip_path) {
  if (!TWFunc::IsPathExists("/system/bin/ozip_decrypt")) {
    return 1;
  }
  gui2_console::info("ozip_decrypt_decryption");
  TWFunc::ExecCmd("ozip_decrypt " + (std::string)TW_OZIP_DECRYPT_KEY + " '" + zip_path + "'");
  gui2_console::info("ozip_decrypt_finish");
  return 0;
}
#endif

// GUIAction::flash_zip
int flash_zip(std::string filename, int* wipe_cache) {
  int ret_val = 0;

  DataManager::SetValue("ui_progress", 0);
  DataManager::SetValue("ui_portion_size", 0);
  DataManager::SetValue("ui_portion_start", 0);

  if (filename.empty()) {
    LOGERR("No file specified.\n");
    return -1;
  }

  if (!TWFunc::IsPathExists(filename)) {
    if (!PartitionManager.MountByPath(filename, true)) {
      return -1;
    }
    if (!TWFunc::IsPathExists(filename)) {
      gui2_console::log_error("unable_to_locate", filename);
      return -1;
    }
  }

  char apex_enabled[PROPERTY_VALUE_MAX];
  property_get("twrp.apex.flattened", apex_enabled, "");
  if (strcmp(apex_enabled, "true") == 0) {
    umount("/apex");
  }
  ret_val = TWinstall_zip(filename.c_str(), wipe_cache,
                          (bool)!DataManager::GetIntValue(TW_SKIP_DIGEST_CHECK_ZIP_VAR));
  PartitionManager.UnlockBlockPartitions();
  // Now, check if we need to ensure TWRP remains installed...
  struct stat st;
  if (stat("/system/bin/installTwrp", &st) == 0) {
    gui2_console::info("config_twrp");
    if (TWFunc::ExecCmd("/system/bin/installTwrp reinstall") < 0) {
      gui2_console::info("config_twrp_err");
    }
  }

  // Done
  DataManager::SetValue("ui_progress", 100);
  DataManager::SetValue("ui_progress", 0);
  DataManager::SetValue("ui_portion_size", 0);
  DataManager::SetValue("ui_portion_start", 0);
  return ret_val;
}

}  // namespace

twrp_install_backend::~twrp_install_backend() {
  if (worker_.joinable()) worker_.join();
}

void twrp_install_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_install_backend::start_zip(const std::vector<std::string>& paths) {
  if (running_.load() || paths.empty()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = install_state::RUNNING;
    status_ = {};
    status_.items = paths;
    started_ = std::chrono::steady_clock::now();
  }
  running_.store(true);
  worker_ = std::thread(&twrp_install_backend::run_zip, this, paths);
  return true;
}

// GUIAction::flash; the queue is gui2's, handed over as it is.
void twrp_install_backend::run_zip(std::vector<std::string> zip_queue) {
  int i, ret_val = 0, wipe_cache = 0;
  const int zip_queue_index = static_cast<int>(zip_queue.size());
  // Stop old scans before the install changes /data.
  PartitionManager.InvalidateAsyncDataSize();
  for (i = 0; i < zip_queue_index; i++) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_.current = i;
    }
    std::string zip_path = zip_queue[i];
    size_t slashpos = zip_path.find_last_of('/');
    std::string zip_filename =
        (slashpos == std::string::npos) ? zip_path : zip_path.substr(slashpos + 1);
    operation_start("Flashing");
#ifdef TW_OZIP_DECRYPT_KEY
    if ((zip_path.substr(zip_path.size() - 4, 4)) == "ozip") {
      if ((ozip_decrypt(zip_path)) != 0) {
        LOGERR("Unable to find ozip_decrypt!");
        ret_val = 1;
        break;
      }
      zip_filename = (zip_filename.substr(0, zip_filename.size() - 4)).append("zip");
      zip_path = (zip_path.substr(0, zip_path.size() - 4)).append("zip");
      if (!TWFunc::IsPathExists(zip_path)) {
        LOGERR("Unable to find decrypted zip");
        ret_val = 1;
        break;
      }
    }
#endif
    DataManager::SetValue("tw_filename", zip_path);
    DataManager::SetValue("tw_file", zip_filename);
    DataManager::SetValue(TW_ZIP_INDEX, (i + 1));

    TWFunc::SetPerformanceMode(true);
    ret_val = flash_zip(zip_path, &wipe_cache);
    TWFunc::SetPerformanceMode(false);
    if (ret_val != 0) {
      gui2_console::log_error("zip_err", zip_path);
      ret_val = 1;
      break;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_.done = i + 1;
    }
  }

  if (wipe_cache) {
    gui2_console::info("zip_wipe_cache");
    PartitionManager.WipeByPath("/cache");
  }

  // Defer the size walk; installation itself has finished.
  PartitionManager.UpdateSystemDetails(true);
  operation_end(ret_val);
  DataManager::SetValue(TW_ZIP_QUEUE_COUNT, 0);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ret_val == 0 ? install_state::DONE : install_state::FAILED;
    status_.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_).count();
  }
  running_.store(false);
}

std::vector<image_target> twrp_install_backend::image_targets() {
  std::vector<PartitionList> list;
  PartitionManager.TryGetPartitionList("flashimg", &list);

  std::vector<image_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list) {
    result.push_back({ entry.display_name, entry.mount_point, entry.slot_select });
  }
  return result;
}

// GUIFileSelector::NotifySelect for a file ending in .img.
std::string twrp_install_backend::image_target_for(const std::string& filename) {
  std::string str_lower = filename;
  std::transform(str_lower.begin(), str_lower.end(), str_lower.begin(), ::tolower);
  if (str_lower == "boot.img" || str_lower == "boot_a.img" || str_lower == "boot_b.img") {
    return "/boot";
  } else if (str_lower == "init_boot.img" || str_lower == "init_boot_a.img" ||
             str_lower == "init_boot_b.img") {
    return "/init_boot";
  } else if (str_lower == "vendor_boot.img" || str_lower == "vendor_boot_a.img" ||
             str_lower == "vendor_boot_b.img") {
    return "/vendor_boot";
  } else if ((str_lower.find("recovery") == 0) || (str_lower.find("twrp") == 0) ||
             (str_lower.find("orangefox") == 0)) {
    return "/recovery";
  } else if (str_lower == "dtbo.img" || str_lower == "dtbo_a.img" || str_lower == "dtbo_b.img") {
    return "/dtbo";
  } else if (str_lower.find("kernelsu_patched_") == 0) {
    std::vector<PartitionList> targets;
    if (!PartitionManager.TryGetPartitionList("flashimg", &targets)) return "";
    if (std::any_of(targets.begin(), targets.end(), [](const PartitionList& target) {
          return target.mount_point == "/init_boot";
        })) {
      return "/init_boot";
    } else {
      return "/boot";
    }
  }
  return "";
}

bool twrp_install_backend::start_image(const std::string& path, const std::string& mount_point,
                                      bool both_slots) {
  if (running_.load() || path.empty()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = install_state::RUNNING;
    status_ = {};
    status_.items = {path};
    started_ = std::chrono::steady_clock::now();
  }
  // FlashImage still consumes the selected partition through DataManager.
  DataManager::SetValue("tw_flash_partition", mount_point + ";");
  DataManager::SetValue("tw_flash_both_slots", both_slots ? 1 : 0);
  running_.store(true);
  worker_ = std::thread(&twrp_install_backend::run_image, this, path, mount_point, both_slots);
  return true;
}

// Image parameters are captured by value when the worker starts.
void twrp_install_backend::run_image(std::string image_path, std::string mount_point,
                                     bool both_slots) {
  int op_status = 0;
  bool flag = true;

  operation_start("Flash Image");
  const size_t slash = image_path.find_last_of('/');
  const std::string path = slash == std::string::npos ? "/" : image_path.substr(0, slash);
  const std::string filename =
      slash == std::string::npos ? image_path : image_path.substr(slash + 1);

#ifdef AB_OTA_UPDATER
  TWPartition* t_part = PartitionManager.FindPartitionByPath(mount_point);

  if (t_part != NULL && (both_slots && t_part->IsSlotSelect())) {
    std::string current_slot = PartitionManager.GetActiveSlotDisplay();
    bool pre_op_status = PartitionManager.FlashImage(path, filename);

    PartitionManager.OverrideActiveSlot(current_slot == "A" ? "B" : "A");
    op_status = (int)!(pre_op_status && PartitionManager.FlashImage(path, filename));
    PartitionManager.OverrideActiveSlot(current_slot);

    flag = false;
  }
#endif
  if (flag) {
    if (PartitionManager.FlashImage(path, filename))
      op_status = 0;  // success
    else
      op_status = 1;  // fail
  }

  operation_end(op_status);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = op_status == 0 ? install_state::DONE : install_state::FAILED;
    status_.done = op_status == 0 ? 1 : 0;
    status_.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_).count();
  }
  running_.store(false);
}

install_status twrp_install_backend::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  install_status result = status_;
  result.state = state_;
  if (state_ == install_state::RUNNING) {
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_).count();
    const int progress = std::atoi(DataManager::GetStrValue("ui_progress").c_str());
    if (progress > 0) result.progress = std::min(progress, 100);
  }
  return result;
}

void twrp_install_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != install_state::RUNNING) state_ = install_state::IDLE;
}

}  // namespace gui2_backend
