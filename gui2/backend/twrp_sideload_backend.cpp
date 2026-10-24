#include "twrp_sideload_backend.h"

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>

#include "data.hpp"
#include "fuse_sideload.h"
#include "gui2/console/stdio_adapter.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twinstall/adb_install.h"
#include "twrp_functions.hpp"
#include "twrp_operation.h"

namespace gui2_backend {

twrp_sideload_backend::~twrp_sideload_backend() {
  if (running_.load()) stop_child();
  if (worker_.joinable()) worker_.join();
  if (canceller_.joinable()) canceller_.join();
}

void twrp_sideload_backend::join_threads() {
  if (running_.load()) return;
  if (worker_.joinable()) worker_.join();
  if (canceller_.joinable()) canceller_.join();
}

bool twrp_sideload_backend::start(bool wipe_dalvik, bool wipe_cache) {
  if (running_.load()) return false;
  join_threads();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = sideload_state::RUNNING;
  }
  cancelled_.store(false);
  // The sideload page's two checkboxes.
  DataManager::SetValue("tw_wipe_dalvik", wipe_dalvik ? 1 : 0);
  DataManager::SetValue("tw_wipe_cache", wipe_cache ? 1 : 0);
  running_.store(true);
  worker_ = std::thread(&twrp_sideload_backend::run, this);
  return true;
}

// GUIAction::adbsideload
void twrp_sideload_backend::run() {
  operation_start("Sideload");
  gui2_console::info("start_sideload");
  bool mtp_was_enabled = TWFunc::ToggleMtp(false);

  // wait for the adb connection
  Device::BuiltinAction reboot_action = Device::REBOOT_BOOTLOADER;
  int ret = twrp_sideload("/", &reboot_action);

  if (ret != 0) {
    if (ret == -2) gui2_console::info("need_new_adb");
    ret = 1;  // failure
  } else {
    int wipe_cache = 0;
    int wipe_dalvik = 0;
    DataManager::GetValue("tw_wipe_dalvik", wipe_dalvik);
    if (wipe_cache || DataManager::GetIntValue("tw_wipe_cache")) PartitionManager.WipeByPath("/cache");
    if (wipe_dalvik) PartitionManager.WipeDalvikCache();
  }
  TWFunc::ToggleMtp(mtp_was_enabled);
  operation_end(ret);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_.load())
      state_ = sideload_state::CANCELLED;
    else
      state_ = ret == 0 ? sideload_state::DONE : sideload_state::FAILED;
  }
  running_.store(false);
}

// GUIAction::adbsideloadcancel
void twrp_sideload_backend::stop_child() {
  struct stat st;
  gui2_console::info("cancel_sideload");
  LOGINFO("Signaling child sideload process to exit.\n");
  // Calling stat() on this magic filename signals the minadbd
  // subprocess to shut down.
  stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
  const pid_t sideload_child_pid = GetMiniAdbdPid();
  if (!sideload_child_pid) {
    LOGERR("Unable to get child ID\n");
    return;
  }
  ::sleep(1);
  LOGINFO("Killing child sideload process.\n");
  kill(sideload_child_pid, SIGTERM);
  int status;
  LOGINFO("Waiting for child sideload process to exit.\n");
  waitpid(sideload_child_pid, &status, 0);
  DataManager::SetValue("tw_page_done", "1");  // For OpenRecoveryScript support
}

void twrp_sideload_backend::cancel() {
  if (!running_.load() || cancelled_.exchange(true)) return;
  // It sleeps and waits for the child; keep that off the UI thread.
  canceller_ = std::thread(&twrp_sideload_backend::stop_child, this);
}

sideload_status twrp_sideload_backend::status() {
  sideload_status result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.state = state_;
  if (state_ == sideload_state::RUNNING) {
    const int progress = std::atoi(DataManager::GetStrValue("ui_progress").c_str());
    if (progress > 0) result.progress = std::min(progress, 100);
  }
  return result;
}

void twrp_sideload_backend::acknowledge() {
  join_threads();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != sideload_state::RUNNING) state_ = sideload_state::IDLE;
}

}  // namespace gui2_backend
