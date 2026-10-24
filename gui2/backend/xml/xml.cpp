/*
	Copyright 2015 _that/TeamWin
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

#include "gui2/console/stdio_adapter.h"
#include "xml.h"
#include "gui2/third_party/rapidxml.hpp"
#include <android-base/file.h>
#include <cstdlib>
#include <cstring>

char* recovery_load_xml(const std::string& filename) {
  std::string data;
  if (!android::base::ReadFileToString(filename, &data)) return nullptr;
  return strdup(data.c_str());
}
void rapidxml::parse_error_handler(const char* what, void*) {
  gui_print("XML parse error: %s\n", what);
  std::abort();
}
