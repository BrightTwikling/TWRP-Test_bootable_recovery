#include "i18n.h"

#include <algorithm>
#include <iterator>

namespace gui2_i18n {

extern const language_pack kEnglish;
extern const language_pack kSimplifiedChinese;
extern const language_pack kTraditionalChinese;
extern const language_pack kRussian;

namespace {
constexpr message_binding bindings[] = {
    {"adopted_storage", &language_pack::adopted_storage},
    {"and_sec_wipe_err", &language_pack::and_sec_wipe_err},
    {"android_secure", &language_pack::android_secure},
    {"auto_generate", &language_pack::auto_generate},
    {"autostorage", &language_pack::autostorage},
    {"available_space", &language_pack::available_space},
    {"avg_backup_fs", &language_pack::avg_backup_fs},
    {"avg_backup_img", &language_pack::avg_backup_img},
    {"backing", &language_pack::backing},
    {"backing_up", &language_pack::backing_up_message},
    {"backup_cancel", &language_pack::backup_cancel},
    {"backup_clean", &language_pack::backup_clean},
    {"backup_complete", &language_pack::backup_complete_message},
    {"backup_completed", &language_pack::backup_completed},
    {"backup_error", &language_pack::backup_error},
    {"backup_fail", &language_pack::backup_fail},
    {"backup_folder", &language_pack::backup_folder},
    {"backup_folder_set", &language_pack::backup_folder_set},
    {"backup_name_exists", &language_pack::backup_name_exists_message},
    {"backup_name_invalid", &language_pack::backup_name_invalid_message},
    {"backup_name_len", &language_pack::backup_name_len},
    {"backup_size", &language_pack::backup_size},
    {"backup_started", &language_pack::backup_started},
    {"backup_storage_undecrypt_warning", &language_pack::backup_storage_undecrypt_warning},
    {"backup_storage_warning", &language_pack::backup_storage_warning},
    {"boot", &language_pack::boot},
    {"cache", &language_pack::cache},
    {"cache_dalvik_done", &language_pack::cache_dalvik_done},
    {"calc_restore", &language_pack::calc_restore},
    {"calculating", &language_pack::calculating},
    {"cancel_sideload", &language_pack::cancel_sideload},
    {"cannot_resize", &language_pack::cannot_resize},
    {"cannot_wipe", &language_pack::cannot_wipe},
    {"change_fs_err", &language_pack::change_fs_err},
    {"check_for_digest", &language_pack::check_for_digest},
    {"cleaned", &language_pack::cleaned},
    {"compression_on", &language_pack::compression_on},
    {"config_twrp", &language_pack::config_twrp},
    {"config_twrp_err", &language_pack::config_twrp_err},
    {"copy_kernel_log", &language_pack::copy_kernel_log},
    {"copy_log", &language_pack::copy_log},
    {"copy_logcat", &language_pack::copy_logcat},
    {"create_folder_strerr", &language_pack::create_folder_strerr},
    {"create_part", &language_pack::create_part},
    {"create_ramdisk_error", &language_pack::create_ramdisk_error},
    {"curr_date", &language_pack::curr_date},
    {"dalvik", &language_pack::dalvik},
    {"dalvik_done", &language_pack::dalvik_done},
    {"dalvik_wipe_err", &language_pack::dalvik_wipe_err},
    {"data", &language_pack::data},
    {"data_backup", &language_pack::data_backup},
    {"data_media_fbe_msg", &language_pack::data_media_fbe_msg},
    {"datamedia_fs_restore", &language_pack::datamedia_fs_restore},
    {"decrypt_cmd", &language_pack::decrypt_cmd},
    {"decrypt_success", &language_pack::decrypt_success},
    {"decrypt_success_dev", &language_pack::decrypt_success_dev},
    {"decrypt_success_nodev", &language_pack::decrypt_success_nodev},
    {"decrypt_user_fail_fbe", &language_pack::decrypt_user_fail_fbe},
    {"decrypt_user_success_fbe", &language_pack::decrypt_user_success_fbe},
    {"decrypting_user_fbe", &language_pack::decrypting_user_fbe},
    {"digest_check_skip", &language_pack::digest_check_skip},
    {"digest_created", &language_pack::digest_created},
    {"digest_error", &language_pack::digest_error},
    {"digest_fail_match", &language_pack::digest_fail_match},
    {"digest_matched", &language_pack::digest_matched},
    {"digest_off", &language_pack::digest_off},
    {"disable_avb2_fail_msg", &language_pack::disable_avb2_fail_msg},
    {"disable_avb2_success_msg", &language_pack::disable_avb2_success_msg},
    {"disabling_AVB2", &language_pack::disabling_AVB2},
    {"done", &language_pack::done},
    {"done_ors", &language_pack::done_ors},
    {"error_opening_strerr", &language_pack::error_opening_strerr},
    {"ext_swap_size", &language_pack::ext_swap_size},
    {"fail_backup_folder", &language_pack::fail_backup_folder},
    {"fail_decrypt_tar", &language_pack::fail_decrypt_tar},
    {"fail_mount", &language_pack::fail_mount},
    {"fail_unmount", &language_pack::fail_unmount},
    {"fastboot_console_msg", &language_pack::fastboot_console_msg},
    {"file_progress", &language_pack::file_progress},
    {"fix_recovery_loop_patch_error", &language_pack::fix_recovery_loop_patch_error},
    {"fixing_recovery_loop_patch", &language_pack::fixing_recovery_loop_patch},
    {"flash_ab_inactive", &language_pack::flash_ab_inactive},
    {"flash_ab_reboot", &language_pack::flash_ab_reboot},
    {"flash_current_twrp_not_supported", &language_pack::flash_current_twrp_not_supported},
    {"flash_done", &language_pack::flash_done},
    {"flash_unable_locate", &language_pack::flash_unable_locate},
    {"flashing", &language_pack::flashing},
    {"format_data_err", &language_pack::format_data_err},
    {"format_data_msg", &language_pack::format_data_msg},
    {"format_sdext_as", &language_pack::format_sdext_as},
    {"formatting_using", &language_pack::formatting_using},
    {"full_selinux", &language_pack::full_selinux},
    {"generating_digest1", &language_pack::generating_digest1},
    {"generating_digest2", &language_pack::generating_digest2},
    {"image_flash_start", &language_pack::image_flash_start},
    {"img_size_err", &language_pack::img_size_err},
    {"img_to_flash", &language_pack::img_to_flash},
    {"install_took_seconds_msg", &language_pack::install_took_seconds_msg},
    {"install_twrp_ramdisk", &language_pack::install_twrp_ramdisk},
    {"installing_zip", &language_pack::installing_zip},
    {"internal", &language_pack::internal},
    {"invalid_flash", &language_pack::invalid_flash},
    {"invalid_zip_format", &language_pack::invalid_zip_format},
    {"locate_backup_err", &language_pack::locate_backup_err},
    {"making_dir1", &language_pack::making_dir1},
    {"making_dir2", &language_pack::making_dir2},
    {"microsd", &language_pack::microsd},
    {"modified_ramdisk_error", &language_pack::modified_ramdisk_error},
    {"mount_vab_partitions", &language_pack::mount_vab_partitions},
    {"mounted", &language_pack::mounted},
    {"mounting", &language_pack::mounting},
    {"mtp_already_enabled", &language_pack::mtp_already_enabled},
    {"mtp_crash", &language_pack::mtp_crash},
    {"mtp_enabled", &language_pack::mtp_enabled},
    {"mtp_fail", &language_pack::mtp_fail},
    {"need_new_adb", &language_pack::need_new_adb},
    {"no_ab_support", &language_pack::no_ab_support},
    {"no_andsec", &language_pack::no_andsec},
    {"no_crypto_support", &language_pack::no_crypto_support},
    {"no_digest", &language_pack::no_digest},
    {"no_kernel_selinux", &language_pack::no_kernel_selinux},
    {"no_mtp", &language_pack::no_mtp},
    {"no_part_flash", &language_pack::no_part_flash},
    {"no_part_restore", &language_pack::no_part_restore},
    {"no_partition_selected", &language_pack::no_partition_selected},
    {"no_pwd", &language_pack::no_pwd},
    {"no_space", &language_pack::no_space},
    {"ors_complete", &language_pack::ors_complete},
    {"ors_encrypt_restore_err", &language_pack::ors_encrypt_restore_err},
    {"ozip_decrypt_decryption", &language_pack::ozip_decrypt_decryption},
    {"ozip_decrypt_finish", &language_pack::ozip_decrypt_finish},
    {"part_complete", &language_pack::part_complete},
    {"partition_sd_locate", &language_pack::partition_sd_locate},
    {"partitions", &language_pack::partitions},
    {"pid_error", &language_pack::pid_error},
    {"pid_signal", &language_pack::pid_signal},
    {"reboot_after_restore", &language_pack::reboot_after_restore},
    {"rebooting", &language_pack::rebooting},
    {"recovery", &language_pack::recovery},
    {"recreate_folder_err", &language_pack::recreate_folder_err},
    {"remove_all", &language_pack::remove_all},
    {"remove_part_table", &language_pack::remove_part_table},
    {"rename_stock", &language_pack::rename_stock},
    {"repack", &language_pack::repack},
    {"repack_error", &language_pack::repack_error},
    {"repack_overwrite_warning", &language_pack::repack_overwrite_warning},
    {"repacking_image", &language_pack::repacking_image},
    {"repair_not_exist", &language_pack::repair_not_exist},
    {"repair_resize", &language_pack::repair_resize},
    {"repairing_using", &language_pack::repairing_using},
    {"rescue_party0", &language_pack::rescue_party0},
    {"rescue_party1", &language_pack::rescue_party1},
    {"rescue_party2", &language_pack::rescue_party2},
    {"rescue_party3", &language_pack::rescue_party3},
    {"rescue_party4", &language_pack::rescue_party4},
    {"resizing", &language_pack::resizing_message},
    {"restore", &language_pack::restore},
    {"restore_completed", &language_pack::restore_completed},
    {"restore_error", &language_pack::restore_error},
    {"restore_folder", &language_pack::restore_folder},
    {"restore_part_count", &language_pack::restore_part_count},
    {"restore_part_done", &language_pack::restore_part_done},
    {"restore_read_only", &language_pack::restore_read_only},
    {"restore_started", &language_pack::restore_started},
    {"restore_system_context", &language_pack::restore_system_context},
    {"restore_unable_locate", &language_pack::restore_unable_locate},
    {"restoring", &language_pack::restoring_message},
    {"restoring_hdr", &language_pack::restoring_hdr},
    {"run_script", &language_pack::run_script},
    {"running_command", &language_pack::running_command},
    {"running_ors", &language_pack::running_ors},
    {"running_recovery_commands", &language_pack::running_recovery_commands},
    {"screenrecord_save_err", &language_pack::screenrecord_save_err},
    {"screenrecord_saved", &language_pack::screenrecord_saved},
    {"screenrecord_start_err", &language_pack::screenrecord_start_err},
    {"screenrecord_started", &language_pack::screenrecord_started},
    {"screenshot_err", &language_pack::screenshot_err},
    {"screenshot_saved", &language_pack::screenshot_saved_message},
    {"sdcard", &language_pack::sdcard},
    {"sdext", &language_pack::sdext},
    {"select_backup_opt", &language_pack::select_backup_opt},
    {"set_restore_opt", &language_pack::set_restore_opt},
    {"setting", &language_pack::setting},
    {"setting_empty", &language_pack::setting_empty},
    {"sideload", &language_pack::sideload},
    {"size_progress", &language_pack::size_progress},
    {"skip_digest", &language_pack::skip_digest},
    {"sparse_import_err", &language_pack::sparse_import_err},
    {"split_backup", &language_pack::split_backup},
    {"split_thread", &language_pack::split_thread},
    {"start_partition_sd", &language_pack::start_partition_sd},
    {"start_sideload", &language_pack::start_sideload},
    {"system", &language_pack::system},
    {"system_image", &language_pack::system_image},
    {"too_many_flash", &language_pack::too_many_flash},
    {"total_backed_size", &language_pack::total_backed_size},
    {"total_backup_size", &language_pack::total_backup_size},
    {"total_partitions_backup", &language_pack::total_partitions_backup},
    {"total_restore_size", &language_pack::total_restore_size},
    {"tw_folder_exists", &language_pack::tw_folder_exists},
    {"unable_find_part_path", &language_pack::unable_find_part_path},
    {"unable_locate_part_backup_name", &language_pack::unable_locate_part_backup_name},
    {"unable_locate_storage", &language_pack::unable_locate_storage},
    {"unable_repair", &language_pack::unable_repair},
    {"unable_resize", &language_pack::unable_resize},
    {"unable_rm_part", &language_pack::unable_rm_part},
    {"unable_set_boot_slot", &language_pack::unable_set_boot_slot},
    {"unable_to_create_part", &language_pack::unable_to_create_part},
    {"unable_to_decrypt", &language_pack::unable_to_decrypt},
    {"unable_to_locate", &language_pack::unable_to_locate},
    {"unable_to_locate_partition", &language_pack::unable_to_locate_partition},
    {"unable_to_mount", &language_pack::unable_to_mount},
    {"unable_to_mount_storage", &language_pack::unable_to_mount_storage},
    {"unable_to_open", &language_pack::unable_to_open},
    {"unable_to_wipe", &language_pack::unable_to_wipe},
    {"unmount_system", &language_pack::unmount_system},
    {"unmount_system_err", &language_pack::unmount_system_err},
    {"unmounted", &language_pack::unmounted},
    {"unmounting", &language_pack::unmounting},
    {"unpack_error", &language_pack::unpack_error},
    {"unpacking_image", &language_pack::unpacking_image},
    {"update_part_details", &language_pack::update_part_details},
    {"update_part_details_done", &language_pack::update_part_details_done},
    {"updating_system_details", &language_pack::updating_system_details},
    {"usbotg", &language_pack::usbotg},
    {"vendor", &language_pack::vendor},
    {"vendor_image", &language_pack::vendor_image},
    {"verify_zip_done", &language_pack::verify_zip_done},
    {"verify_zip_fail", &language_pack::verify_zip_fail},
    {"verify_zip_sig", &language_pack::verify_zip_sig},
    {"verifying_digest", &language_pack::verifying_digest},
    {"wiping", &language_pack::wiping_message},
    {"wiping_cache_dalvik", &language_pack::wiping_cache_dalvik},
    {"wiping_dalvik", &language_pack::wiping_dalvik},
    {"wiping_data", &language_pack::wiping_data},
    {"wiping_datamedia", &language_pack::wiping_datamedia},
    {"zip_compatible_err", &language_pack::zip_compatible_err},
    {"zip_err", &language_pack::zip_err},
    {"zip_wipe_cache", &language_pack::zip_wipe_cache},
};

const message_binding* find_message(std::string_view key) {
  const auto entry = std::ranges::lower_bound(bindings, key, {}, &message_binding::key);
  return entry != std::end(bindings) && entry->key == key ? entry : nullptr;
}

std::string_view message_value(language_id language, const message_binding& binding) {
  const char* value = get_language_pack(language).*binding.member;
  return value == nullptr ? std::string_view{} : std::string_view(value);
}
}  // namespace

std::span<const message_binding> message_bindings() {
  return bindings;
}

std::string_view lookup_message(language_id language, std::string_view key) {
  const auto* binding = find_message(key);
  if (binding == nullptr) return {};
  const auto value = message_value(language, *binding);
  return value.empty() ? message_value(language_id::ENGLISH, *binding) : value;
}

bool matches_message(std::string_view key, std::string_view value) {
  if (value.empty()) return false;
  const auto* binding = find_message(key);
  if (binding == nullptr) return false;
  for (const auto language : kLanguageValues) {
    if (message_value(language, *binding) == value) return true;
  }
  return false;
}

const language_pack& get_language_pack(language_id language) {
  switch (language) {
    case language_id::ZH_CN:
      return kSimplifiedChinese;
    case language_id::ZH_TW:
      return kTraditionalChinese;
    case language_id::RUSSIAN:
      return kRussian;
    case language_id::ENGLISH:
    default:
      return kEnglish;
  }
}

const char* language_code(language_id language) {
  switch (language) {
    case language_id::ZH_CN:
      return "zh_CN";
    case language_id::ZH_TW:
      return "zh_TW";
    case language_id::RUSSIAN:
      return "ru";
    case language_id::ENGLISH:
    default:
      return "en";
  }
}

language_id language_from_code(std::string_view code) {
  for (const auto language : kLanguageValues) {
    if (code == language_code(language)) return language;
  }
  return language_id::ENGLISH;
}

}  // namespace gui2_i18n
