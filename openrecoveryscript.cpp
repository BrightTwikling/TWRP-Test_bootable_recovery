/*
	Copyright 2003 to 2021 TeamWin
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

#define __STDC_FORMAT_MACROS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iterator>
#include <algorithm>
#include <sys/types.h>
#include <sys/wait.h>
#include <zlib.h>
#include <cutils/properties.h>

#include "twrp_functions.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "openrecoveryscript.hpp"
#include "progresstracking.hpp"
#include "variables.h"
#include "data.hpp"
#include "fuse_sideload.h"
#include "gui2/console/stdio_adapter.h"
#include "orscmd/orscmd.h"
#include "twinstall.h"
#include "twinstall/adb_install.h"

OpenRecoveryScript::VoidFunction OpenRecoveryScript::call_after_cli_command;

#define SCRIPT_COMMAND_SIZE 512

int OpenRecoveryScript::check_for_script_file(void) {
	std::string logDir = TWFunc::GetLogDir();
	std::string orsFile;
	if (logDir == DATA_LOGS_DIR)
		orsFile = "/data/cache";
	else
		orsFile = logDir;
	orsFile += "/recovery/openrecoveryscript";
	if (!PartitionManager.MountByPath(orsFile, false)) {
		LOGINFO("Unable to mount %s for OpenRecoveryScript support.\n", logDir.c_str());
		gui2_console::log_error("unable_to_mount", logDir.c_str());
		return 0;
	}
	if (TWFunc::IsPathExists(orsFile)) {
		LOGINFO("Script file found: '%s'\n", orsFile.c_str());
		// Copy script file to /tmp
		TWFunc::CopyFile(orsFile, SCRIPT_FILE_TMP, 0755);
		// Delete the file from cache
		unlink(orsFile.c_str());
		return 1;
	}
	return 0;
}

int OpenRecoveryScript::copy_script_file(string filename) {
	if (TWFunc::IsPathExists(filename)) {
		LOGINFO("Script file found: '%s'\n", filename.c_str());
		if (filename == SCRIPT_FILE_TMP)
			return 1; // file is already in the right place
		// Copy script file to /tmp
		TWFunc::CopyFile(filename, SCRIPT_FILE_TMP, 0755);
		// Delete the old file
		unlink(filename.c_str());
		return 1;
	}
	return 0;
}

int OpenRecoveryScript::run_script_file(void) {
	int ret_val = 0, cindex, line_len, i, remove_nl, install_cmd = 0, sideload = 0;
	char script_line[SCRIPT_COMMAND_SIZE], command[SCRIPT_COMMAND_SIZE],
	     value[SCRIPT_COMMAND_SIZE], mount[SCRIPT_COMMAND_SIZE],
	     value1[SCRIPT_COMMAND_SIZE], value2[SCRIPT_COMMAND_SIZE];
	char *val_start, *tok;

	FILE *fp = fopen(SCRIPT_FILE_TMP, "r");
	if (fp != NULL) {
		DataManager::SetValue(TW_SIMULATE_ACTIONS, 0);
		DataManager::SetValue("ui_progress", 0); // Reset the progress bar
		while (fgets(script_line, SCRIPT_COMMAND_SIZE, fp) != NULL && ret_val == 0) {
			cindex = 0;
			line_len = strlen(script_line);
			if (line_len < 2)
				continue; // there's a blank line or line is too short to contain a command
			//gui_print("script line: '%s'\n", script_line);
			for (i=0; i<line_len; i++) {
				if ((int)script_line[i] == 32) {
					cindex = i;
					i = line_len;
				}
			}
			memset(command, 0, sizeof(command));
			memset(value, 0, sizeof(value));
			if ((int)script_line[line_len - 1] == 10)
				remove_nl = 2;
			else
				remove_nl = 1;
			if (cindex != 0) {
				strncpy(command, script_line, cindex);
				LOGINFO("command is: '%s'\n", command);
				val_start = script_line;
				val_start += cindex + 1;
				if ((int) *val_start == 32)
					val_start++; //get rid of space
				if ((int) *val_start == 51)
					val_start++; //get rid of = at the beginning
				if ((int) *val_start == 32)
					val_start++; //get rid of space
				strncpy(value, val_start, line_len - cindex - remove_nl);
				LOGINFO("value is: '%s'\n", value);
			} else {
				strncpy(command, script_line, line_len - remove_nl + 1);
				gui_print("command is: '%s' and there is no value\n", command);
			}
			if (strcmp(command, "install") == 0) {
				// Install Zip
				DataManager::SetValue("tw_action_text2", "Installing Zip");
				ret_val = Install_Command(value);
				install_cmd = -1;
			} else if (strcmp(command, "wipe") == 0) {
				// Wipe
				if (strcmp(value, "cache") == 0 || strcmp(value, "/cache") == 0) {
					PartitionManager.WipeByPath("/cache");
				} else if (strcmp(value, "system") == 0 || strcmp(value, "/system") == 0 || strcmp(value, PartitionManager.GetAndroidRootPath().c_str()) == 0) {
					PartitionManager.WipeByPath("/system");
					PartitionManager.UpdateSystemDetails();
				} else if (strcmp(value, "dalvik") == 0 || strcmp(value, "dalvick") == 0 || strcmp(value, "dalvikcache") == 0 || strcmp(value, "dalvickcache") == 0) {
					PartitionManager.WipeDalvikCache();
				} else if (strcmp(value, "data") == 0 || strcmp(value, "/data") == 0 || strcmp(value, "factory") == 0 || strcmp(value, "factoryreset") == 0) {
					PartitionManager.FactoryReset();
				} else {
					LOGERR("Error with wipe command value: '%s'\n", value);
					ret_val = 1;
				}
			} else if (strcmp(command, "format") == 0) {
				// Format
				if (strcmp(value, "data") == 0 || strcmp(value, "/data") == 0 || strcmp(value, "factory") == 0 || strcmp(value, "factoryreset") == 0) {
					PartitionManager.FormatData();
				} else {
					LOGERR("Error with format command value: '%s'\n", value);
					ret_val = 1;
				}
			} else if (strcmp(command, "backup") == 0) {
				// Backup
				DataManager::SetValue("tw_action_text2", gui2_console::text("backing"));
				tok = strtok(value, " ");
				strcpy(value1, tok);
				tok = strtok(NULL, " ");
				if (tok != NULL) {
					memset(value2, 0, sizeof(value2));
					strcpy(value2, tok);
					line_len = strlen(tok);
					if ((int)value2[line_len - 1] == 10 || (int)value2[line_len - 1] == 13) {
						if ((int)value2[line_len - 1] == 10 || (int)value2[line_len - 1] == 13)
							remove_nl = 2;
						else
							remove_nl = 1;
					} else
						remove_nl = 0;
					strncpy(value2, tok, line_len - remove_nl);
					DataManager::SetValue(TW_BACKUP_NAME, value2);
					gui2_console::info("backup_folder_set", value2);
					if (!TWPartitionManager::IsBackupNameValid(value2, true, true)) {
						ret_val = 1;
						continue;
					}
				} else {
					char empt[50];
					strcpy(empt, "(Current Date)");
					DataManager::SetValue(TW_BACKUP_NAME, empt);
				}
				ret_val = Backup_Command(value1);
			} else if (strcmp(command, "restore") == 0) {
				// Restore
				DataManager::SetValue("tw_action_text2", gui2_console::text("restore"));
				PartitionManager.MountAllStorage();
				DataManager::SetValue(TW_SKIP_DIGEST_CHECK_VAR, 0);
				char folder_path[512], partitions[512];

				string val = value, restore_folder, restore_partitions;
				size_t pos = val.find_last_of(" ");
				if (pos == string::npos) {
					restore_folder = value;
					partitions[0] = '\0';
				} else {
					restore_folder = val.substr(0, pos);
					restore_partitions = val.substr(pos + 1, val.size() - pos - 1);
					strcpy(partitions, restore_partitions.c_str());
				}
				strcpy(folder_path, restore_folder.c_str());
				LOGINFO("Restore folder is: '%s' and partitions: '%s'\n", folder_path, partitions);
				gui2_console::info("restoring", folder_path);

				if (folder_path[0] != '/') {
					char backup_folder[512];
					string folder_var;
					std::vector<PartitionList> Storage_List;

					PartitionManager.GetPartitionList("storage", &Storage_List);
					int listSize = Storage_List.size();
					for (int i = 0; i < listSize; i++) {
						if (PartitionManager.IsMountedByPath(Storage_List.at(i).mount_point)) {
							DataManager::SetValue("tw_storage_path", Storage_List.at(i).mount_point);
							DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, folder_var);
							sprintf(backup_folder, "%s/%s", folder_var.c_str(), folder_path);
							if (TWFunc::IsPathExists(backup_folder)) {
								strcpy(folder_path, backup_folder);
								break;
							}
						}
					}
				} else {
					if (folder_path[strlen(folder_path) - 1] == '/')
						strcat(folder_path, ".");
					else
						strcat(folder_path, "/.");
				}
				if (!TWFunc::IsPathExists(folder_path)) {
					gui2_console::log_error("locate_backup_err", folder_path);
					ret_val = 1;
					continue;
				}
				DataManager::SetValue("tw_restore", folder_path);

				PartitionManager.SetRestoreFiles(folder_path);
				string Partition_List;
				int is_encrypted = 0;
				DataManager::GetValue("tw_restore_encrypted", is_encrypted);
				DataManager::GetValue("tw_restore_list", Partition_List);
				if (strlen(partitions) != 0) {
					string Restore_List;

					memset(value2, 0, sizeof(value2));
					strcpy(value2, partitions);
					gui2_console::info("set_restore_opt", value2);
					line_len = strlen(value2);
					for (i=0; i<line_len; i++) {
						if ((value2[i] == 'S' || value2[i] == 's') && Partition_List.find("/system;") != string::npos) {
							Restore_List += "/system;";
							gui2_console::info("system");
						} else if ((value2[i] == 'D' || value2[i] == 'd') && Partition_List.find("/data;") != string::npos) {
							Restore_List += "/data;";
							gui2_console::info("data");
						} else if ((value2[i] == 'C' || value2[i] == 'c') && Partition_List.find("/cache;") != string::npos) {
							Restore_List += "/cache;";
							gui2_console::info("cache");
						} else if ((value2[i] == 'R' || value2[i] == 'r') && Partition_List.find("/recovery;") != string::npos) {
							Restore_List += "/recovery;";
							gui2_console::info("recovery");
						} else if ((value2[i] == 'B' || value2[i] == 'b') && Partition_List.find("/boot;") != string::npos) {
							Restore_List += "/boot;";
							gui2_console::info("boot");
						} else if ((value2[i] == 'A' || value2[i] == 'a')  && Partition_List.find("/and-sec;") != string::npos) {
							Restore_List += "/and-sec;";
							gui2_console::info("android_secure");
						} else if ((value2[i] == 'E' || value2[i] == 'e')  && Partition_List.find("/sd-ext;") != string::npos) {
							Restore_List += "/sd-ext;";
							gui2_console::info("sdext");
						} else if (value2[i] == 'M' || value2[i] == 'm') {
							DataManager::SetValue(TW_SKIP_DIGEST_CHECK_VAR, 1);
							gui2_console::info("digest_check_skip");
						}
					}

					DataManager::SetValue("tw_restore_selected", Restore_List);
				} else {
					DataManager::SetValue("tw_restore_selected", Partition_List);
				}
				if (is_encrypted) {
					gui2_console::log_error("ors_encrypt_restore_err");
					ret_val = 1;
				} else if (!PartitionManager.RunRestore(folder_path))
					ret_val = 1;
				else
					gui2_console::info("done");
			} else if (strcmp(command, "remountrw") == 0) {
				ret_val = remountrw();
			} else if (strcmp(command, "mount") == 0) {
				// Mount
				DataManager::SetValue("tw_action_text2", gui2_console::text("mounting"));
				if (value[0] != '/') {
					strcpy(mount, "/");
					strcat(mount, value);
				} else
					strcpy(mount, value);
				if (!strcmp(mount, "/system"))
					strcpy(mount, PartitionManager.GetAndroidRootPath().c_str());
				if (PartitionManager.MountByPath(mount, true))
					gui2_console::info("mounted", mount);
			} else if (strcmp(command, "unmount") == 0 || strcmp(command, "umount") == 0) {
				// Unmount
				DataManager::SetValue("tw_action_text2", gui2_console::text("unmounting"));
				if (value[0] != '/') {
					strcpy(mount, "/");
					strcat(mount, value);
				} else
					strcpy(mount, value);
				if (!strcmp(mount, "/system"))
					strcpy(mount, PartitionManager.GetAndroidRootPath().c_str());
				if (PartitionManager.UnmountByPath(mount, true))
					gui2_console::info("unmounted", mount);
			} else if (strcmp(command, "set") == 0) {
				// Set value
				size_t len = strlen(value);
				tok = strtok(value, " ");
				strcpy(value1, tok);
				if (len > strlen(value1) + 1) {
					char *val2 = value + strlen(value1) + 1;
					gui2_console::info("setting", value1, val2);
					DataManager::SetValue(value1, val2);
				} else {
					gui2_console::info("setting_empty", value1);
					DataManager::SetValue(value1, "");
				}
			} else if (strcmp(command, "mkdir") == 0) {
				// Make directory (recursive)
				DataManager::SetValue("tw_action_text2", gui2_console::text("making_dir1"));
				gui2_console::info("making_dir2", value);
				if (!TWFunc::RecursiveMkdir(value)) {
					// error message already displayed by RecursiveMkdir
					ret_val = 1;
				}
			} else if (strcmp(command, "reboot") == 0) {
				if (strlen(value) && strcmp(value, "recovery") == 0)
					TWFunc::TwReboot(RECOVERY);
				else if (strlen(value) && strcmp(value, "poweroff") == 0)
					TWFunc::TwReboot(POWER_OFF);
				else if (strlen(value) && strcmp(value, "bootloader") == 0)
					TWFunc::TwReboot(BOOTLOADER);
				else if (strlen(value) && strcmp(value, "download") == 0)
					TWFunc::TwReboot(DOWNLOAD);
				else if (strlen(value) && strcmp(value, "edl") == 0)
					TWFunc::TwReboot(EDL);
				else
					TWFunc::TwReboot(SYSTEM);
			} else if (strcmp(command, "cmd") == 0) {
				DataManager::SetValue("tw_action_text2", gui2_console::text("running_command"));
				if (cindex != 0) {
					TWFunc::ExecCmd(value);
				} else {
					LOGERR("No value given for cmd\n");
				}
			} else if (strcmp(command, "print") == 0) {
				gui_print("%s\n", value);
			} else if (strcmp(command, "sideload") == 0) {
				// ADB Sideload
				DataManager::SetValue("tw_action_text2", gui2_console::text("sideload"));
				install_cmd = -1;

				int wipe_cache = 0;
				string result;

				gui2_console::info("start_sideload");

				Device::BuiltinAction reboot_action = Device::REBOOT_BOOTLOADER;
				ret_val = twrp_sideload("/", &reboot_action);
				if (ret_val != 0) {
					if (ret_val == -2)
						gui2_console::log_error("need_new_adb");
					ret_val = 1; // failure
				} else if (TWinstall_zip(FUSE_SIDELOAD_HOST_PATHNAME, &wipe_cache) == 0) {
					if (wipe_cache)
						PartitionManager.WipeByPath("/cache");
				} else {
					ret_val = 1; // failure
				}
				PartitionManager.UnlockBlockPartitions();
				PartitionManager.UpdateSystemDetails();
				sideload = 1; // Causes device to go to the home screen afterwards
				pid_t sideload_child_pid = GetMiniAdbdPid();
				if (sideload_child_pid != 0) {
					LOGINFO("Signaling child sideload process to exit.\n");
					struct stat st;
					// Calling stat() on this magic filename signals the minadbd
					// subprocess to shut down.
					stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
					int status;
					LOGINFO("Waiting for child sideload process to exit.\n");
					waitpid(sideload_child_pid, &status, 0);
				}
				property_set("ctl.start", "adbd");
				gui2_console::info("done");
			} else if (strcmp(command, "fixperms") == 0 || strcmp(command, "fixpermissions") == 0 || strcmp(command, "fixcontexts") == 0) {
				ret_val = PartitionManager.FixContexts();
				if (ret_val != 0)
					ret_val = 1; // failure
			} else if (strcmp(command, "decrypt") == 0) {
				// twrp cmd cannot decrypt a password with space, should decrypt on gui
				if (*value) {
					string tmp = value;
					std::vector<string> args = TWFunc::SplitString(tmp, " ");

					string pass = args[0];
					string userid = "0";
					if (args.size() > 1)
						userid = args[1];

					ret_val = PartitionManager.DecryptDevice(pass, atoi(userid.c_str()));
					if (ret_val != 0)
						ret_val = 1;  // failure
				} else {
					gui2_console::log_error("no_pwd");
					ret_val = 1;  // failure
				}
			} else if (strcmp(command, "listmounts") == 0) {
				TWFunc::ListMounts();
			} else {
				LOGERR("Unrecognized script command: '%s'\n", command);
				ret_val = 1;
			}
		}
		fclose(fp);
		unlink(SCRIPT_FILE_TMP);
		gui2_console::info("done_ors");
	} else {
		gui2_console::log_error("error_opening_strerr", SCRIPT_FILE_TMP, strerror(errno));
		return 1;
	}

	if (sideload)
		ret_val = 1;  // Forces booting to the home page after sideload
	return ret_val;
}

int OpenRecoveryScript::Insert_ORS_Command(string Command) {
	ofstream ORSfile(SCRIPT_FILE_TMP, ios_base::app | ios_base::out);
	if (ORSfile.is_open()) {
		LOGINFO("Inserting '%s'\n", Command.c_str());
		ORSfile << Command.c_str() << endl;
		ORSfile.close();
		return 1;
	}
	LOGERR("Unable to append '%s' to '%s'\n", Command.c_str(), SCRIPT_FILE_TMP);
	return 0;
}

int OpenRecoveryScript::Install_Command(string Zip) {
	// Install zip
	string ret_string;
	int ret_val = 0, wipe_cache = 0;
	std::vector<PartitionList> Storage_List;
	string Full_Path;

	if (Zip.substr(0, 1) == "@") {
		// This is a special file that contains a map of blocks on the data partition
		Full_Path = Zip.substr(1);
		if (!PartitionManager.MountByPath(Full_Path, true) || !TWFunc::IsPathExists(Full_Path)) {
			LOGINFO("Unable to install via mapped zip '%s'\n", Full_Path.c_str());
			gui2_console::log_error("zip_err", Zip);
			return 1;
		}
		LOGINFO("Installing mapped zip file '%s'\n", Full_Path.c_str());
		gui2_console::info("installing_zip", Zip);
	} else if (!TWFunc::IsPathExists(Zip)) {
		PartitionManager.MountAllStorage();
		PartitionManager.GetPartitionList("storage", &Storage_List);
		int listSize = Storage_List.size();
		for (int i = 0; i < listSize; i++) {
			if (PartitionManager.IsMountedByPath(Storage_List.at(i).mount_point)) {
				Full_Path = Storage_List.at(i).mount_point + "/" + Zip;
				if (TWFunc::IsPathExists(Full_Path)) {
					Zip = Full_Path;
					break;
				}
				Full_Path = Zip;
				LOGINFO("Trying to find zip '%s' on '%s'...\n", Full_Path.c_str(), Storage_List.at(i).mount_point.c_str());
				ret_string = Locate_Zip_File(Full_Path, Storage_List.at(i).mount_point);
				if (!ret_string.empty()) {
					Zip = ret_string;
					break;
				}
			}
		}
		if (!TWFunc::IsPathExists(Zip)) {
			// zip file doesn't exist
			gui_print("Unable to locate zip file '%s'.\n", Zip.c_str());
			ret_val = 1;
		} else
			gui2_console::info("installing_zip", Zip);
	}

	ret_val = TWinstall_zip(Zip.c_str(), &wipe_cache);
	if (ret_val != 0) {
		gui2_console::log_error("zip_err", Zip);
		ret_val = 1;
	} else if (wipe_cache)
		PartitionManager.WipeByPath("/cache");

	return ret_val;
}

string OpenRecoveryScript::Locate_Zip_File(string Zip, string Storage_Root) {
	string Path = TWFunc::GetPath(Zip);
	string File = fs::path(Zip).filename().string();
	string pathCpy = Path;
	string wholePath;
	size_t pos = Path.find("/", 1);

	while (pos != string::npos)
	{
		pathCpy = Path.substr(pos, Path.size() - pos);
		wholePath = pathCpy + File;
		LOGINFO("Looking for zip at '%s'\n", wholePath.c_str());
		if (TWFunc::IsPathExists(wholePath))
			return wholePath;
		wholePath = Storage_Root + wholePath;
		LOGINFO("Looking for zip at '%s'\n", wholePath.c_str());
		if (TWFunc::IsPathExists(wholePath))
			return wholePath;

		pos = Path.find("/", pos + 1);
	}
	return "";
}

int OpenRecoveryScript::Backup_Command(string Options) {
	char value1[SCRIPT_COMMAND_SIZE];
	int line_len, i;
	string Backup_List;

	strcpy(value1, Options.c_str());

	DataManager::SetValue(TW_USE_COMPRESSION_VAR, 0);
	DataManager::SetValue(TW_SKIP_DIGEST_GENERATE_VAR, 0);

	gui2_console::info("select_backup_opt");
	line_len = Options.size();
	for (i=0; i<line_len; i++) {
		if (Options.substr(i, 1) == "S" || Options.substr(i, 1) == "s") {
			Backup_List += "/system;";
			gui2_console::info("system");
		} else if (Options.substr(i, 1) == "D" || Options.substr(i, 1) == "d") {
			Backup_List += "/data;";
			gui2_console::info("data");
		} else if (Options.substr(i, 1) == "C" || Options.substr(i, 1) == "c") {
			Backup_List += "/cache;";
			gui2_console::info("cache");
		} else if (Options.substr(i, 1) == "R" || Options.substr(i, 1) == "r") {
			Backup_List += "/recovery;";
			gui2_console::info("recovery");
		} else if (Options.substr(i, 1) == "1") {
			gui_print("%s\n", "Special1 -- No Longer Supported...");
		} else if (Options.substr(i, 1) == "2") {
			gui_print("%s\n", "Special2 -- No Longer Supported...");
		} else if (Options.substr(i, 1) == "3") {
			gui_print("%s\n", "Special3 -- No Longer Supported...");
		} else if (Options.substr(i, 1) == "B" || Options.substr(i, 1) == "b") {
			Backup_List += "/boot;";
			gui2_console::info("boot");
		} else if (Options.substr(i, 1) == "A" || Options.substr(i, 1) == "a") {
			Backup_List += "/and-sec;";
			gui2_console::info("android_secure");
		} else if (Options.substr(i, 1) == "E" || Options.substr(i, 1) == "e") {
			Backup_List += "/sd-ext;";
			gui2_console::info("sdext");
		} else if (Options.substr(i, 1) == "O" || Options.substr(i, 1) == "o") {
			DataManager::SetValue(TW_USE_COMPRESSION_VAR, 1);
			gui2_console::info("compression_on");
		} else if (Options.substr(i, 1) == "M" || Options.substr(i, 1) == "m") {
			DataManager::SetValue(TW_SKIP_DIGEST_GENERATE_VAR, 1);
			gui2_console::info("digest_off");
		}
	}
	DataManager::SetValue("tw_backup_list", Backup_List);
	if (!PartitionManager.RunBackup()) {
		gui2_console::log_error("backup_fail");
		return 1;
	}
	gui2_console::info("backup_complete");
	return 0;
}

int OpenRecoveryScript::Run_OpenRecoveryScript_Action() {
	int op_status = 1;
	// Check for the SCRIPT_FILE_TMP first as these are AOSP recovery commands
	// that we converted to ORS commands during boot in recovery.cpp.
	// Run those first.
	int reboot = 0;
	if (TWFunc::IsPathExists(SCRIPT_FILE_TMP)) {
		gui2_console::info("running_recovery_commands");
		if (OpenRecoveryScript::run_script_file() == 0) {
			reboot = 1;
			op_status = 0;
		}
	}
	// Check for the ORS file in /cache and attempt to run those commands.
	if (OpenRecoveryScript::check_for_script_file()) {
		gui2_console::info("running_ors");
		if (OpenRecoveryScript::run_script_file() == 0) {
			reboot = 1;
			op_status = 0;
		}
	}
	if (reboot) {
		// Disable stock recovery reflashing
		TWFunc::DisableStockRecoveryReplace();
		usleep(2000000); // Sleep for 2 seconds before rebooting
		TWFunc::TwReboot(SYSTEM);
		usleep(5000000); // Sleep for 5 seconds to allow reboot to occur
	} else {
		DataManager::SetValue("tw_page_done", 1);
	}
	return op_status;
}

// Runs commands received by the background ORS FIFO listener.
void OpenRecoveryScript::Run_CLI_Command(const char* command) {
	string tmp = command;
	std::vector<string> parts =
		TWFunc::SplitString(tmp, " ");  // pats[0] is cmd, parts[1...] is args
	string cmd_str = parts[0];

	if (cmd_str == "runscript") {
		if (parts.size() > 1) {
			string filename = parts[1];
			if (OpenRecoveryScript::copy_script_file(filename) == 0) {
				LOGINFO("Unable to copy script file\n");
			} else {
				OpenRecoveryScript::run_script_file();
			}
		} else {
			LOGINFO("Missing parameter: script file name\n");
		}
	} else if (cmd_str == "get") {
		if (parts.size() > 1) {
			string varname = parts[1];
			string value;
			DataManager::GetValue(varname, value);
			gui_print("%s = %s\n", varname.c_str(), value.c_str());
		} else {
			LOGINFO("Missing parameter: var name\n");
		}
	} else if (cmd_str == "decrypt") {
		// twrp cmd cannot decrypt a password with space, should decrypt on gui
		if (parts.size() == 1) {
			gui2_console::log_error("no_pwd");
		} else {
			string pass = parts[1];
			string userid = "0";
			if (parts.size() > 2)
				userid = parts[2];

			gui2_console::info("decrypt_cmd");
			if (PartitionManager.DecryptDevice(pass, atoi(userid.c_str())) == 0) {
				// set_page_done = 1;  // done by singleaction_page anyway
				std::string orsFile = TWFunc::GetLogDir() + "/openrecoveryscript";
				if (TWFunc::IsPathExists(orsFile)) {
					Run_OpenRecoveryScript_Action();
				}
			}
		}
	} else if (OpenRecoveryScript::Insert_ORS_Command(command)) {
		OpenRecoveryScript::run_script_file();
	}

	// let the GUI close the output fd and restart the command listener
	call_after_cli_command();
	LOGINFO("Done reading ORS command from command line\n");
}

int OpenRecoveryScript::remountrw(void)
{
	bool remount_system = PartitionManager.IsMountedByPath(PartitionManager.GetAndroidRootPath());
	int op_status;
	TWPartition* part;

	if (!PartitionManager.UnmountByPath(PartitionManager.GetAndroidRootPath(), true)) {
		op_status = 1; // fail
	} else {
		part = PartitionManager.FindPartitionByPath(PartitionManager.GetAndroidRootPath());
		if (part) {
			DataManager::SetValue("tw_mount_system_ro", 0);
			part->ChangeMountReadOnly(false);
		}
		if (remount_system) {
			part->Mount(true);
		}
		op_status = 0; // success
	}

	return op_status;
}
