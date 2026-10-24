/*
	Copyright 2013 to 2020 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <format>
#include <string>
#include <sys/mount.h>
#include <sys/wait.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp_functions.hpp"
#include "twrpRepacker.hpp"
#include "twcommon.h"
#include "variables.h"
#include "gui2/console/stdio_adapter.h"

#ifdef AB_OTA_UPDATER
#include <BootControlClient.h>
using android::hal::BootControlClient;
#endif

bool twrpRepacker::PrepareEmptyFolder(const std::string& Folder) {
	if (TWFunc::IsPathExists(Folder))
		TWFunc::RemoveDir(Folder, false);
	return TWFunc::RecursiveMkdir(Folder);
}

bool twrpRepacker::Backup_Image_For_Repack(TWPartition* part, const std::string& Temp_Folder_Destination,
										 const bool Create_Backup, const std::string& backup_name) {
	if (!part) {
		LOGERR("Partition was null!\n");
		return false;
	}
	if (!PrepareEmptyFolder(Temp_Folder_Destination))
		return false;
	std::string target_image = Temp_Folder_Destination + "boot.img";
	PartitionSettings part_settings;
	part_settings.part = part;
	if (Create_Backup) {
		if (!TWPartitionManager::IsBackupNameValid(backup_name, true, false))
			return false;
		DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, part_settings.backup_folder);
		part_settings.backup_folder = part_settings.backup_folder + "/" + TWFunc::GetCurrentDate() + " " + backup_name + "/";
		if (!TWFunc::RecursiveMkdir(part_settings.backup_folder))
			return false;
	} else
		part_settings.backup_folder = Temp_Folder_Destination;
	part_settings.generate_digest = false;
	part_settings.pm_method = PartitionManagerOp::kBackup;
	part_settings.progress = NULL;
	pid_t not_a_pid = 0;
	if (!part->Backup(&part_settings, &not_a_pid))
		return false;
	std::string backed_up_image = part_settings.backup_folder;
	backed_up_image += part->GetBackupFileName();
	target_image = Temp_Folder_Destination + "boot.img";
	if (Create_Backup) {
		std::string source = part_settings.backup_folder + part->GetBackupFileName();
		if (TWFunc::CopyFile(source, target_image, 0644) != 0) {
			LOGERR("Failed to copy backup file '%s' to temp folder target '%s'\n", source.c_str(), target_image.c_str());
			return false;
		}
	} else {
		if (rename(backed_up_image.c_str(), target_image.c_str()) != 0) {
			LOGERR("Failed to rename '%s' to '%s'\n", backed_up_image.c_str(), target_image.c_str());
			return false;
		}
	}
	original_ramdisk_format = Unpack_Image(target_image, Temp_Folder_Destination, false, false);
	return !original_ramdisk_format.empty();
}

std::string twrpRepacker::Unpack_Image(const std::string& Source_Path, const std::string& Temp_Folder_Destination,
										const bool Copy_Source, const bool Create_Destination) {
	std::string txt_to_find = "RAMDISK_FMT";
	if (Create_Destination) {
		if (!PrepareEmptyFolder(Temp_Folder_Destination))
			return std::string();
	}
	if (Copy_Source) {
		std::string destination = Temp_Folder_Destination + "/boot.img";
		if (TWFunc::CopyFile(Source_Path, destination, 0644))
			return std::string();
	}
	std::string command = "cd " + Temp_Folder_Destination + " && /system/bin/magiskboot unpack -h -n ";
	command = command + "'" + Source_Path +"'";

	std::string magisk_unpack_output;
	int ret;
	// RETURN_VENDOR (3) is expected when unpacking vendor_boot.
	if ((ret = TWFunc::ExecCmd(command, magisk_unpack_output, true)) != 0 &&
		(!WIFEXITED(ret) || WEXITSTATUS(ret) != 3)) {
		LOGINFO("Error unpacking %s, ret: %d!\n", Source_Path.c_str(), ret);
		gui2_console::log_error("unpack_error");
		return std::string();
	}
	std::string ramdisk_format;
	auto pos = magisk_unpack_output.find(txt_to_find);
	if (pos != std::string::npos) {
		auto start = magisk_unpack_output.find('[', pos + txt_to_find.size());
			if (start != std::string::npos) {
				auto end = magisk_unpack_output.find(']', start);
				if (end != std::string::npos) {
					ramdisk_format = magisk_unpack_output.substr(start + 1, end - start - 1);
				}
		}
	}
	if (ramdisk_format.empty()) {
		// v4 reports each vendor ramdisk separately; use recovery's format.
		const std::string marker = "VND_RAMDISK name=[recovery]";
		pos = magisk_unpack_output.find(marker);
		if (pos == std::string::npos)
			pos = magisk_unpack_output.find("VND_RAMDISK");
		if (pos != std::string::npos) {
			const std::string fmt_marker = "fmt=[";
			const auto start = magisk_unpack_output.find(fmt_marker, pos);
			if (start != std::string::npos) {
				const auto end = magisk_unpack_output.find(']', start + fmt_marker.size());
				if (end != std::string::npos)
					ramdisk_format = magisk_unpack_output.substr(start + fmt_marker.size(), end - start - fmt_marker.size());
			}
		}
	}
	return ramdisk_format;
}

bool twrpRepacker::Repack_Image_And_Flash(const std::string& Target_Image, const struct RepackOptionsStruct& Repack_Options) {
	if (!TWFunc::IsPathExists("/system/bin/magiskboot")) {
		LOGERR("Image repacking tool not present in this TWRP build!");
		return false;
	}

	bool recompress = false;
	bool is_vendor_boot = false;
	bool is_vendor_boot_v4 = false;
	std::string dest_partition = "/boot";
	std::string ramdisk_cpio = "ramdisk.cpio";

	#ifdef BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT
		dest_partition = "/vendor_boot";
		is_vendor_boot = true;
		if (DataManager::GetIntValue("tw_boot_header_version") == 4) {
			is_vendor_boot_v4 = true;
			ramdisk_cpio = "vendor_ramdisk/recovery.cpio";
			LOGINFO("Vendor_boot with v4 header\n");
		} else {
			LOGINFO("Vendor_boot with v3 header\n");
		}
	#else
		// we shouldn't reach here, because of the code in twrpRepacker::Flash_Current_Twrp(); but if we do, then handle it
		if (PartitionManager.FindPartitionByPath("/recovery") && !DataManager::GetStrValue("tw_active_slot").empty()) {
			dest_partition = "/recovery";
		}
	#endif

	if (is_vendor_boot || is_vendor_boot_v4) {
		// placeholder for any specific vendor_boot stuff;
		// in the meantime, stop the compiler's complaints about unused variables
	}

	DataManager::SetProgress(0);
	PartitionManager.UpdateSystemDetails();
	TWPartition* part = PartitionManager.FindPartitionByPath(dest_partition);
	if (part)
		gui2_console::info("unpacking_image", part->GetDisplayName());
	else {
		gui2_console::log_error("unable_to_locate", dest_partition.c_str());
		return false;
	}
	if (!Backup_Image_For_Repack(part, REPACK_ORIG_DIR, Repack_Options.Backup_First, gui_lookup("repack", "Repack")))
		return false;
	DataManager::SetProgress(.25);
	if (Repack_Options.Type == REPLACE_RAMDISK_UNPACKED) {
		if (!PrepareEmptyFolder(REPACK_NEW_DIR))
			return false;
		if (is_vendor_boot_v4 && !TWFunc::RecursiveMkdir(std::string(REPACK_NEW_DIR) + "vendor_ramdisk"))
			return false;
		image_ramdisk_format = "gzip";
	} else {
		gui2_console::info("unpacking_image", Target_Image);
		image_ramdisk_format = Unpack_Image(Target_Image, REPACK_NEW_DIR, true);
	}
	if (image_ramdisk_format.empty())
		return false;
	DataManager::SetProgress(.5);
	gui2_console::info("repacking_image", part->GetDisplayName());
	std::string path = REPACK_NEW_DIR;
	if (Repack_Options.Type == REPLACE_KERNEL) {
		// When we replace the kernel, what we really do is copy the boot partition ramdisk into the new image's folder
		if (TWFunc::CopyFile(REPACK_ORIG_DIR + ramdisk_cpio, REPACK_NEW_DIR + ramdisk_cpio, 0644)) {
			LOGERR("Failed to copy ramdisk\n");
			return false;
		}
	} else if (Repack_Options.Type == REPLACE_RAMDISK_UNPACKED) {
			if (TWFunc::CopyFile(Target_Image, REPACK_ORIG_DIR + ramdisk_cpio, 0644)) {
				LOGERR("Failed to copy ramdisk\n");
				return false;
			}
			if (TWFunc::CopyFile(Target_Image, REPACK_NEW_DIR + ramdisk_cpio, 0644)) {
				LOGERR("Failed to copy ramdisk\n");
				return false;
			}
		path = REPACK_ORIG_DIR;
	} else if (Repack_Options.Type == REPLACE_RAMDISK) {
		// Repack the ramdisk
		if (TWFunc::CopyFile(REPACK_NEW_DIR + ramdisk_cpio, REPACK_ORIG_DIR + ramdisk_cpio, 0644)) {
			LOGERR("Failed to copy ramdisk\n");
			return false;
		}
		path = REPACK_ORIG_DIR;
	} else {
		LOGERR("Invalid repacking options specified\n");
		return false;
	}
	if (Repack_Options.Disable_Verity)
		LOGERR("Disabling verity is not implemented yet\n");
	if (Repack_Options.Disable_Force_Encrypt)
		LOGERR("Disabling force encrypt is not implemented yet\n");
	std::string command = "cd " + path + " && /system/bin/magiskboot repack ";
	if (original_ramdisk_format != image_ramdisk_format) {
		recompress = true;
	}

	command += path + "boot.img";

	std::string orig_compressed_image(REPACK_ORIG_DIR);
	orig_compressed_image += ramdisk_cpio;
	std::string copy_compressed_image(REPACK_ORIG_DIR);
	copy_compressed_image += "ramdisk-1.cpio";

	if (recompress) {
		std::string decompress_cmd = "/system/bin/magiskboot decompress " + orig_compressed_image + " " + copy_compressed_image;
		if (TWFunc::ExecCmd(decompress_cmd) != 0) {
			gui2_console::log_error("repack_error");
			return false;
		}
		std::rename(copy_compressed_image.c_str(), orig_compressed_image.c_str());
	}

	if (TWFunc::ExecCmd(command) != 0) {
		gui2_console::log_error("repack_error");
		return false;
	}

	DataManager::SetProgress(.75);
	std::string file = "new-boot.img";
	DataManager::SetValue("tw_flash_partition", dest_partition + ";");
	if (!PartitionManager.FlashImage(path, file)) {
		LOGINFO("Error flashing new image\n");
		return false;
	}
	DataManager::SetProgress(1);
	TWFunc::RemoveDir(REPACK_ORIG_DIR, false);
	if (part->IsSlotSelect()) {
		if (Repack_Options.Type == REPLACE_RAMDISK || Repack_Options.Type == REPLACE_RAMDISK_UNPACKED) {
			LOGINFO("Switching slots to flash ramdisk to both partitions\n");
			const string Current_Slot = PartitionManager.GetActiveSlotDisplay();
			struct SlotRestorer {
				~SlotRestorer() { PartitionManager.OverrideActiveSlot(slot); }
				std::string slot;
			} restorer{Current_Slot};
			if (Current_Slot == "A")
				PartitionManager.OverrideActiveSlot("B");
			else
				PartitionManager.OverrideActiveSlot("A");
			DataManager::SetProgress(.25);
			if (!Backup_Image_For_Repack(part, REPACK_ORIG_DIR, Repack_Options.Backup_First, gui_lookup("repack", "Repack")))
				return false;
			if (TWFunc::CopyFile(REPACK_NEW_DIR + ramdisk_cpio, REPACK_ORIG_DIR + ramdisk_cpio, 0644)) {
				LOGERR("Failed to copy ramdisk\n");
				return false;
			}
			path = REPACK_ORIG_DIR;
			std::string command = "cd " + path + " && /system/bin/magiskboot repack ";

			if (original_ramdisk_format != image_ramdisk_format) {
				recompress = true;
			}
			command += path + "boot.img";

			if (recompress) {
				std::string decompress_cmd = "/system/bin/magiskboot decompress " + orig_compressed_image + " " + copy_compressed_image;
				if (TWFunc::ExecCmd(decompress_cmd) != 0) {
					gui2_console::log_error("repack_error");
					return false;
				}
				std::rename(copy_compressed_image.c_str(), orig_compressed_image.c_str());
			}

			if (TWFunc::ExecCmd(command) != 0) {
				gui2_console::log_error("repack_error");
				return false;
			}
			DataManager::SetProgress(.75);
			std::string file = "new-boot.img";
			DataManager::SetValue("tw_flash_partition", dest_partition + ";");
			if (!PartitionManager.FlashImage(path, file)) {
				LOGINFO("Error flashing new image\n");
				return false;
			}
			DataManager::SetProgress(1);
			TWFunc::RemoveDir(REPACK_ORIG_DIR, false);
		}
	}
	TWFunc::RemoveDir(REPACK_NEW_DIR, false);
	if (dest_partition == "/boot")
		gui2_console::warning("repack_overwrite_warning");
	return true;
}

bool twrpRepacker::Flash_Current_Twrp() {
#ifdef AB_OTA_UPDATER
	// A/B with dedicated recovery partition
	auto module = BootControlClient::WaitForService();
	if (module == nullptr) {
		LOGERR("Boot control HAL unavailable\n");
		return false;
	}
	int32_t slot = module->GetCurrentSlot();
	
	if (PartitionManager.FindPartitionByPath("/recovery")) {
		std::string dest_partition = "/recovery";
		std::string root = "/dev/block/bootdevice/by-name/recovery";

		std::string src_slot = "a", dst_slot = "b";
		if (slot != 0) std::swap(src_slot, dst_slot);

		PartitionManager.UnlockBlockPartitions();

		// only copy the relevant active slot to the inactive slot, on the basis that the recovery currently running
		// in the active slot can simply be copied over to the inactive slot, so that both have the same recovery image
		std::string command = std::format("dd bs=1048576 if={0}_{1} of={0}_{2}", root, src_slot, dst_slot);
		LOGINFO("Command=%s\n", command.c_str());

		if (TWFunc::ExecCmd(command) != 0) {
			LOGERR("Failed to flash the %s image\n", dest_partition.c_str());
			return false;
		}
		else {
			gui_print("Finished flashing the %s image\n", dest_partition.c_str());
			return true;
		}
		// if we reach here, something is awry - bale out
		return false;
	}
#else
	if (PartitionManager.FindPartitionByPath("/recovery")) {
		gui2_console::log_error("flash_current_twrp_not_supported");
		return false;
	}
#endif // AB_OTA_UPDATER

	if (!TWFunc::IsPathExists("/ramdisk-files.txt")) {
			LOGERR("can not find ramdisk-files.txt");
			return false;
	}
	if (PartitionManager.IsMountedByPath("/vendor") && !PartitionManager.UnmountByPath("/vendor", false)) {
		// Try to force umount /vendor
		PartitionManager.UnmountByPath("/vendor", false, MNT_FORCE|MNT_DETACH);
	}
	RepackOptionsStruct Repack_Options;
	Repack_Options.Disable_Verity = false;
	Repack_Options.Disable_Force_Encrypt = false;
	Repack_Options.Type = REPLACE_RAMDISK_UNPACKED;
	Repack_Options.Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0;
	std::string verifyfiles = "cd / && sha256sum --status -c ramdisk-files.sha256sum";
	if (TWFunc::ExecCmd(verifyfiles) != 0) {
		gui2_console::log_error("modified_ramdisk_error");
		return false;
	}
	std::string command = "cd / && /system/bin/cpio -H newc -o < ramdisk-files.txt > /tmp/currentramdisk.cpio && /system/bin/gzip -f /tmp/currentramdisk.cpio";
	if (TWFunc::ExecCmd(command) != 0) {
		gui2_console::log_error("create_ramdisk_error");
		return false;
	}
	if (!Repack_Image_And_Flash("/tmp/currentramdisk.cpio.gz", Repack_Options))
		return false;
	else
		return true;
}
