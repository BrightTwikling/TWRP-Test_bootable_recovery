/*
	Copyright 2020 TeamWin
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

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>
#include <format>

#include <android-base/strings.h>

#include "data.hpp"
#include "objects.hpp"
#include "partitions.hpp"
#include "rapidxml.hpp"
#include "twcommon.h"
#include "twrpminui/minui.h"
#include "unit_conversion.hpp"
#include "variables.h"

GUIPartitionList::GUIPartitionList(rapidxml::xml_node<>* node) : GUIScrollList(node)
{
	rapidxml::xml_attribute<>* attr;
	rapidxml::xml_node<>* child;

	mIconSelected = mIconUnselected = NULL;
	mUpdate = 0;
	updateList = false;

	child = FindNode(node, "icon");
	if (child)
	{
		mIconSelected = LoadAttrImage(child, "selected");
		mIconUnselected = LoadAttrImage(child, "unselected");
	}

	// Handle the result variable
	child = FindNode(node, "data");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("selectedlist");
		if (attr)
			selectedList = attr->value();
	}

	int iconWidth = 0, iconHeight = 0;
	if (mIconSelected && mIconSelected->GetResource() && mIconUnselected && mIconUnselected->GetResource()) {
		iconWidth = std::max(mIconSelected->GetWidth(), mIconUnselected->GetWidth());
		iconHeight = std::max(mIconSelected->GetHeight(), mIconUnselected->GetHeight());
	} else if (mIconSelected && mIconSelected->GetResource()) {
		iconWidth = mIconSelected->GetWidth();
		iconHeight = mIconSelected->GetHeight();
	} else if (mIconUnselected && mIconUnselected->GetResource()) {
		iconWidth = mIconUnselected->GetWidth();
		iconHeight = mIconUnselected->GetHeight();
	}
	SetMaxIconSize(iconWidth, iconHeight);

	child = FindNode(node, "listtype");
	if (child && (attr = child->first_attribute("name"))) {
		ListType = attr->value();
		updateList = true;
	} else {
		mList.clear();
		LOGERR("No partition listtype specified for partitionlist GUI element\n");
		return;
	}
}

GUIPartitionList::~GUIPartitionList()
{
}

int GUIPartitionList::Update(void)
{
	if (!isConditionTrue())
		return 0;

	// Check for changes in mount points if the list type is mount and update the list and render if needed
	if (ListType == "mount") {
		for (PartitionList& partition : mList) {
			if (PartitionManager.IsMountedByPath(partition.mount_point) && !partition.selected) {
				partition.selected = true;
				mUpdate = 1;
			} else if (!PartitionManager.IsMountedByPath(partition.mount_point) && partition.selected) {
				partition.selected = false;
				mUpdate = 1;
			}
		}
	}

	GUIScrollList::Update();

	if (updateList) {
		// Completely update the list if needed -- Used primarily for
		// restore as the list for restore will change depending on what
		// partitions were backed up
		mList.clear();
		PartitionManager.GetPartitionList(ListType, &mList);
		SetVisibleListLocation(0);
		updateList = false;
		mUpdate = 1;
		if (ListType == "backup" || ListType == "flashimg")
			MatchList();
	}

	if (mUpdate) {
		mUpdate = 0;
		return 2;
	}

	return 0;
}

int GUIPartitionList::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIScrollList::NotifyVarChange(varName, value);

	if (!isConditionTrue())
		return 0;

	// A deferred data/media walk landed, so the sizes on screen are stale.
	if (ListType == "backup" && varName == TW_BACKUP_SIZES_READY) {
		updateList = true;
		mUpdate = 1;
		return 0;
	}

	if (varName == mVariable && !mUpdate)
	{
		if (ListType == "storage") {
			currentValue = value;
			SetPosition();
		} else if (ListType == "backup") {
			updateList = true;
			MatchList();
		} else if (ListType == "restore") {
			updateList = true;
			SetVisibleListLocation(0);
		}

		mUpdate = 1;
		return 0;
	}
	return 0;
}

void GUIPartitionList::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		if (ListType == "storage" || ListType == "flashimg") {
			DataManager::GetValue(mVariable, currentValue);
			SetPosition();
		}
		updateList = true;
		mUpdate = 1;
	}
}

void GUIPartitionList::MatchList(void) {
	std::string variablelist;
	DataManager::GetValue(mVariable, variablelist);

	std::vector<std::string> selected_paths = android::base::Tokenize(variablelist, ";");
	for (PartitionList& partition : mList) {
		auto it = std::find(selected_paths.begin(), selected_paths.end(), partition.mount_point);
		partition.selected = it != selected_paths.end();
		if (partition.selected) {
			TWPartition* t_part = PartitionManager.FindPartitionByPath(partition.mount_point);
			DataManager::SetValue("tw_is_slot_part", t_part != nullptr ? (int) t_part->slot_select_ : 0);
		}
	}
}

void GUIPartitionList::SetPosition() {
	int listSize = mList.size();

	SetVisibleListLocation(0);
	for (int i = 0; i < listSize; i++) {
		if (mList[i].mount_point == currentValue) {
			mList[i].selected = true;
			SetVisibleListLocation(i);
		} else {
			mList[i].selected = false;
		}
	}
}

size_t GUIPartitionList::GetItemCount()
{
	return mList.size();
}

void GUIPartitionList::RenderItem(size_t itemindex, int yPos, bool selected)
{
	// note: the "selected" parameter above is for the currently touched item
	// don't confuse it with the more persistent "selected" flag per list item used below
	ImageResource* icon = mList[itemindex].selected ? mIconSelected : mIconUnselected;
	const std::string& text = mList[itemindex].display_name;

	RenderStdItem(yPos, selected, icon, text.c_str());
}

void GUIPartitionList::NotifySelect(size_t item_selected)
{
	if (item_selected < mList.size()) {
		PartitionList& selected_partition = mList[item_selected];
		if (ListType == "mount") {
			if (!selected_partition.selected) {
				if (PartitionManager.MountByPath(selected_partition.mount_point, true)) {
					selected_partition.selected = true;
					PartitionManager.AddMtpStorage(selected_partition.mount_point);
					mUpdate = 1;
				}
			} else {
				if (PartitionManager.UnmountByPath(selected_partition.mount_point, true)) {
					selected_partition.selected = false;
					mUpdate = 1;
				}
			}
		} else if (!mVariable.empty()) {
			if (ListType == "storage") {
				TWPartition* part = PartitionManager.FindPartitionByPath(selected_partition.mount_point);
				if (part == nullptr) {
					LOGERR("Unable to locate partition for '%s'\n", selected_partition.mount_point.c_str());
					return;
				}
				bool update_size = !part->IsMounted() && part->removable_;
				if (!part->Mount(true))
					return;
				if (update_size && !part->UpdateSize(true))
					return;

				for (PartitionList& partition : mList)
					partition.selected = false;

				if (update_size) {
					selected_partition.display_name = std::format("{} ({})", part->storage_name_, UnitConversion::FormatBytes(part->free_));
				}
				selected_partition.selected = true;
				mUpdate = 1;
				DataManager::SetValue(mVariable, selected_partition.mount_point);
			} else {
				if (ListType == "flashimg") { // only one item can be selected for flashing images
					for (PartitionList& partition : mList)
						partition.selected = false;
				}
				selected_partition.selected = !selected_partition.selected;
				if (selected_partition.selected) {
					TWPartition* t_part = PartitionManager.FindPartitionByPath(selected_partition.mount_point);
					DataManager::SetValue("tw_is_slot_part", t_part != nullptr ? (int) t_part->slot_select_ : 0);
				}

				std::string variablelist;
				for (const PartitionList& partition : mList) {
					if (partition.selected)
						variablelist += partition.mount_point + ";";
				}

				mUpdate = 1;
				if (selectedList.empty())
					DataManager::SetValue(mVariable, variablelist);
				else
					DataManager::SetValue(selectedList, variablelist);
			}
		}
	}
}
