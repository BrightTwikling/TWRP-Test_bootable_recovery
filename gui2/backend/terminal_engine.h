/*
	Copyright 2026 TeamWin
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

#ifndef GUI2_BACKEND_TERMINAL_ENGINE_H
#define GUI2_BACKEND_TERMINAL_ENGINE_H

#include <cstddef>
#include <string>

// Recovery owns one PTY and text buffer, shared across GUI2 page changes.
namespace twrp_terminal {

// Starts the shell if it is not already running.
void start();
bool running();
bool alternate_screen();
bool foreground_job();
int cursor_x();
int cursor_y();
void stop();

// In characters. The engine wraps and scrolls against this.
void set_size(int columns, int rows);

// Drain available shell output without blocking the GUI2 loop.
void pump();

// Changes whenever the buffer changed, so a front end can skip redrawing.
int update_counter();

size_t line_count();
std::string line(size_t index);

void input_char(int codepoint);
void input_key(int key);

}  // namespace twrp_terminal

#endif  // GUI2_BACKEND_TERMINAL_ENGINE_H
