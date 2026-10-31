/*
	Copyright 2012 - 2020 TeamWin
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

// console.cpp - GUIConsole object

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gui.hpp"
#include "objects.hpp"
#include "rapidxml.hpp"
#include "twcommon.h"
#include "twmsg.h"
#include "twrpminui/minui.h"

#define GUI_CONSOLE_BUFFER_SIZE 512

static pthread_mutex_t console_lock;
static size_t last_message_count = 0;
static std::vector<Message> gMessages;

static std::vector<std::string> gConsole;
static std::vector<std::string> gConsoleColor;
// Bumped whenever gConsole is rebuilt, so a reader holding a cursor knows its
// line indexes no longer point at what it read before.
static size_t gConsoleGeneration = 0;
static FILE* ors_file = NULL;

struct InitMutex
{
	InitMutex() { pthread_mutex_init(&console_lock, NULL); }
} initMutex;

static void internal_gui_print(const char *color, char *buf)
{
	// make sure to flush any outstanding messages first to preserve order of outputs
	GUIConsole::Translate_Now();

	fputs(buf, stdout);
	if (ors_file) {
		fprintf(ors_file, "%s", buf);
		fflush(ors_file);
	}

	char *start, *next;

	if (buf[0] == '\n' && strlen(buf) < 2) {
		// This prevents the double lines bug seen in the console during zip installs
		return;
	}

	pthread_mutex_lock(&console_lock);
	for (start = next = buf; *next != '\0';)
	{
		if (*next == '\n')
		{
			*next = '\0';
			gConsole.push_back(start);
			gConsoleColor.push_back(color);

			start = ++next;
		}
		else
			++next;
	}

	// The text after last \n (or whole string if there is no \n)
	if (*start) {
		gConsole.push_back(start);
		gConsoleColor.push_back(color);
	}
	pthread_mutex_unlock(&console_lock);
}

void gui_print(const char *fmt, ...)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, GUI_CONSOLE_BUFFER_SIZE, fmt, ap);
	va_end(ap);

	internal_gui_print("normal", buf);
}

void gui_print_color(const char *color, const char *fmt, ...)
{
	char buf[GUI_CONSOLE_BUFFER_SIZE];		// We're going to limit a single request to 512 bytes

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, GUI_CONSOLE_BUFFER_SIZE, fmt, ap);
	va_end(ap);

	internal_gui_print(color, buf);
}

void gui_set_FILE(FILE* f)
{
	ors_file = f;
}

void gui_msg(const char* text)
{
	if (text) {
		Message msg = Msg(text);
		gui_msg(msg);
	}
}

void gui_warn(const char* text)
{
	if (text) {
		Message msg = Msg(msg::kWarning, text);
		gui_msg(msg);
	}
}

void gui_err(const char* text)
{
	if (text) {
		Message msg = Msg(msg::kError, text);
		gui_msg(msg);
	}
}

void gui_highlight(const char* text)
{
	if (text) {
		Message msg = Msg(msg::kHighlight, text);
		gui_msg(msg);
	}
}

void gui_msg(Message msg)
{
	std::string output = msg;
	output += "\n";
	fputs(output.c_str(), stdout);
	if (ors_file) {
		fprintf(ors_file, "%s", output.c_str());
		fflush(ors_file);
	}
	pthread_mutex_lock(&console_lock);
	gMessages.push_back(msg);
	pthread_mutex_unlock(&console_lock);
}

// Turns one queued message into the console's translated line. Called with the
// console lock held.
static void append_message_locked(size_t index)
{
	Message& msg = gMessages[index];
	std::string message = msg;
	std::string color = "normal";
	if (msg.GetKind() == msg::kError)
		color = "error";
	else if (msg.GetKind() == msg::kHighlight)
		color = "highlight";
	else if (msg.GetKind() == msg::kWarning)
		color = "warning";
	gConsole.push_back(std::move(message));
	gConsoleColor.push_back(std::move(color));
}

void GUIConsole::Translate_Now()
{
	pthread_mutex_lock(&console_lock);
	const size_t message_count = gMessages.size();
	for (size_t m = last_message_count; m < message_count; m++)
		append_message_locked(m);
	last_message_count = message_count;
	pthread_mutex_unlock(&console_lock);
}

void GUIConsole::Clear_For_Retranslation()
{
	pthread_mutex_lock(&console_lock);
	last_message_count = 0;
	gConsole.clear();
	gConsoleColor.clear();
	gConsoleGeneration++;
	pthread_mutex_unlock(&console_lock);
}

size_t GUIConsole::Get_Lines(size_t from, std::vector<std::string>* lines,
							 std::vector<std::string>* colors)
{
	Translate_Now();

	pthread_mutex_lock(&console_lock);
	const size_t total = gConsole.size();
	if (from > total)
		from = total;
	for (size_t i = from; i < total; i++) {
		if (lines)
			lines->push_back(gConsole[i]);
		if (colors)
			colors->push_back(i < gConsoleColor.size() ? gConsoleColor[i] : "normal");
	}
	pthread_mutex_unlock(&console_lock);
	return total;
}

namespace {

bool is_utf8_continuation(char byte)
{
	return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

// Bytes of text[start, ...) that fit in max_bytes without cutting a UTF-8
// sequence. Invalid sequences still advance. A remaining budget below four
// bytes may return zero; the caller retries that character in the next batch.
size_t utf8_prefix_bytes(const std::string& text, size_t start, size_t max_bytes)
{
	const size_t available = text.size() - start;
	if (max_bytes >= available)
		return available;

	const size_t end = start + max_bytes;
	if (!is_utf8_continuation(text[end]))
		return max_bytes;

	// end sits inside a sequence, and a UTF-8 sequence is at most 4 bytes long,
	// so its lead byte is within these three bytes.
	for (size_t back = 1; back < 4 && back <= max_bytes; back++) {
		if (!is_utf8_continuation(text[end - back]))
			return max_bytes - back;
	}
	// No sequence starts inside the budget: the bytes are not UTF-8, so they go
	// out as they are instead of stalling the read.
	return max_bytes;
}

// A cursor produced by Read_Lines sits on a character boundary for valid UTF-8;
// one that came from elsewhere is used as given, because moving it would either
// skip bytes of a sequence that is not UTF-8 or repeat bytes already handed out.
size_t clamp_offset(const std::string& text, size_t offset)
{
	return offset > text.size() ? text.size() : offset;
}

} // namespace

GUIConsoleReadResult GUIConsole::Read_Lines(const GUIConsoleCursor& cursor,
											const GUIConsoleReadLimits& limits,
											std::vector<GUIConsoleLine>* lines)
{
	GUIConsoleReadResult result;
	result.cursor = cursor;

	if (pthread_mutex_trylock(&console_lock) != 0) {
		// A writer has the lock. Report it and leave the cursor alone so the GUI
		// keeps its place and tries again on a later frame instead of waiting
		// here for the printer to finish.
		result.busy = true;
		return result;
	}

	result.cursor.generation = gConsoleGeneration;
	if (cursor.generation != gConsoleGeneration) {
		// The buffer was rebuilt for a retranslation. Everything the caller has
		// is stale, so reading starts over at the first line; the new generation
		// tells the caller to drop what it drew.
		result.cursor.line = 0;
		result.cursor.offset = 0;
	}

	if (limits.max_messages > 0 && last_message_count < gMessages.size()) {
		const size_t batch = std::min(gMessages.size() - last_message_count, limits.max_messages);
		for (size_t m = 0; m < batch; m++)
			append_message_locked(last_message_count + m);
		last_message_count += batch;
	}

	const size_t total = gConsole.size();
	size_t line = result.cursor.line;
	size_t offset = result.cursor.offset;
	if (line >= total) {
		line = total;
		offset = 0;
	} else {
		offset = clamp_offset(gConsole[line], offset);
	}

	// A budget smaller than the longest UTF-8 sequence cannot hold a character,
	// so nothing is copied and the cursor stays where it is.
	const bool can_copy = lines != NULL && limits.max_lines > 0 && limits.max_bytes >= 4;
	size_t entries = 0;
	size_t bytes = 0;

	while (can_copy && entries < limits.max_lines && bytes < limits.max_bytes && line < total) {
		const std::string& text = gConsole[line];
		if (offset >= text.size() && !text.empty()) {
			// A cursor that points at the end of a line that was already handed
			// out continues with the next line instead of producing an empty piece.
			line++;
			offset = 0;
			continue;
		}

		const size_t take = text.empty() ? 0 : utf8_prefix_bytes(text, offset, limits.max_bytes - bytes);
		if (take == 0 && !text.empty())
			break; // the next character needs a fresh batch's byte budget

		GUIConsoleLine entry;
		entry.text = text.substr(offset, take);
		entry.color = line < gConsoleColor.size() ? gConsoleColor[line] : "normal";
		entry.line_index = line;
		entry.offset = offset;
		entry.continued = offset + take < text.size();
		const bool continued = entry.continued;
		lines->push_back(std::move(entry));

		entries++;
		bytes += take;
		if (continued) {
			offset += take;
		} else {
			line++;
			offset = 0;
		}
	}

	result.cursor.line = line;
	result.cursor.offset = offset;
	result.total = total;
	// The cursor only moves past what was handed out, so a full budget leaves the
	// backlog for the next call instead of skipping it.
	result.has_more = line < total || last_message_count < gMessages.size();
	pthread_mutex_unlock(&console_lock);
	return result;
}

GUIConsole::GUIConsole(rapidxml::xml_node<>* node) : GUIScrollList(node)
{
	rapidxml::xml_node<>* child;

	mLastCount = 0;
	scrollToEnd = true;
	mSlideoutX = mSlideoutY = mSlideoutW = mSlideoutH = 0;
	mSlideout = 0;
	mSlideoutState = visible;

	allowSelection = false;	// console doesn't support list item selections

	if (!node)
	{
		mRenderX = 0;
		mRenderY = 0;
		mRenderW = gr_fb_width();
		mRenderH = gr_fb_height();
	}
	else
	{
		child = FindNode(node, "color");
		if (child)
		{
			mFontColor = LoadAttrColor(child, "foreground", mFontColor);
			mBackgroundColor = LoadAttrColor(child, "background", mBackgroundColor);
			//mScrollColor = LoadAttrColor(child, "scroll", mScrollColor);
		}

		child = FindNode(node, "slideout");
		if (child)
		{
			mSlideout = 1;
			mSlideoutState = hidden;
			LoadPlacement(child, &mSlideoutX, &mSlideoutY, &mSlideoutW, &mSlideoutH, &mPlacement);

			mSlideoutImage = LoadAttrImage(child, "resource");

			if (mSlideoutImage && mSlideoutImage->GetResource())
			{
				mSlideoutW = mSlideoutImage->GetWidth();
				mSlideoutH = mSlideoutImage->GetHeight();
				if (mPlacement == CENTER || mPlacement == CENTER_X_ONLY) {
					mSlideoutX = mSlideoutX - (mSlideoutW / 2);
					if (mPlacement == CENTER) {
						mSlideoutY = mSlideoutY - (mSlideoutH / 2);
					}
				}
			}
		}
	}
}

int GUIConsole::RenderSlideout(void)
{
	if (!mSlideoutImage || !mSlideoutImage->GetResource())
		return -1;

	gr_blit(mSlideoutImage->GetResource(), 0, 0, mSlideoutW, mSlideoutH, mSlideoutX, mSlideoutY);
	return 0;
}

int GUIConsole::RenderConsole(void)
{
	Translate_Now();
	pthread_mutex_lock(&console_lock);
	AddLines(&gConsole, &gConsoleColor, &mLastCount, &rConsole, &rConsoleColor);
	pthread_mutex_unlock(&console_lock);
	GUIScrollList::Render();

	// if last line is fully visible, keep tracking the last line when new lines are added
	int bottom_offset = GetDisplayRemainder() - actualItemHeight;
	bool isAtBottom = firstDisplayedItem == (int)GetItemCount() - GetDisplayItemCount() - (bottom_offset != 0) && y_offset == bottom_offset;
	if (isAtBottom)
		scrollToEnd = true;
#if 0
	// debug - show if we are tracking the last line
	if (scrollToEnd) {
		gr_color(0,255,0,255);
		gr_fill(mRenderX+mRenderW-5, mRenderY+mRenderH-5, 5, 5);
	} else {
		gr_color(255,0,0,255);
		gr_fill(mRenderX+mRenderW-5, mRenderY+mRenderH-5, 5, 5);
	}
#endif
	return (mSlideout ? RenderSlideout() : 0);
}

int GUIConsole::Render(void)
{
	if (!isConditionTrue())
		return 0;

	if (mSlideout && mSlideoutState == hidden)
		return RenderSlideout();

	return RenderConsole();
}

int GUIConsole::Update(void)
{
	if (mSlideout && mSlideoutState != visible)
	{
		if (mSlideoutState == hidden)
			return 0;

		if (mSlideoutState == request_hide)
			mSlideoutState = hidden;

		if (mSlideoutState == request_show)
			mSlideoutState = visible;

		// Any time we activate the console, we reset the position
		SetVisibleListLocation(rConsole.size() - 1);
		mUpdate = 1;
		scrollToEnd = true;
	}

	pthread_mutex_lock(&console_lock);
	bool addedNewText = AddLines(&gConsole, &gConsoleColor, &mLastCount, &rConsole, &rConsoleColor);
	pthread_mutex_unlock(&console_lock);
	if (addedNewText) {
		// someone added new text
		// at least the scrollbar must be updated, even if the new lines are currently not visible
		mUpdate = 1;
	}

	if (scrollToEnd) {
		// keep the last line in view
		SetVisibleListLocation(rConsole.size() - 1);
	}

	GUIScrollList::Update();

	if (mUpdate) {
		mUpdate = 0;
		return 2;
	}
	return 0;
}

// IsInRegion - Checks if the request is handled by this object
//  Return 1 if this object handles the request, 0 if not
int GUIConsole::IsInRegion(int x, int y)
{
	if (mSlideout) {
		// Check if they tapped the slideout button
		if (x >= mSlideoutX && x < mSlideoutX + mSlideoutW && y >= mSlideoutY && y < mSlideoutY + mSlideoutH)
			return 1;

		// If we're only rendering the slideout, bail now
		if (mSlideoutState == hidden)
			return 0;
	}

	return GUIScrollList::IsInRegion(x, y);
}

// NotifyTouch - Notify of a touch event
//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
int GUIConsole::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if (!isConditionTrue())
		return -1;

	if (mSlideout && x >= mSlideoutX && x < mSlideoutX + mSlideoutW && y >= mSlideoutY && y < mSlideoutY + mSlideoutH) {
		if (state == TOUCH_START) {
			if (mSlideoutState == hidden)
				mSlideoutState = request_show;
			else if (mSlideoutState == visible)
				mSlideoutState = request_hide;
		}
		return 1;
	}
	scrollToEnd = false;
	return GUIScrollList::NotifyTouch(state, x, y);
}

size_t GUIConsole::GetItemCount()
{
	return rConsole.size();
}

void GUIConsole::RenderItem(size_t itemindex, int yPos, bool selected __unused)
{
	// Set the color for the font
	if (rConsoleColor[itemindex] == "normal") {
		gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);
	} else {
		COLOR FontColor;
		std::string color = rConsoleColor[itemindex];
		ConvertStrToColor(color, &FontColor);
		FontColor.alpha = 255;
		gr_color(FontColor.red, FontColor.green, FontColor.blue, FontColor.alpha);
	}

	// render text
	const char* text = rConsole[itemindex].c_str();
	gr_textEx_scaleW(mRenderX, yPos, text, mFont->GetResource(), mRenderW, TOP_LEFT, 0);
}

void GUIConsole::NotifySelect(size_t item_selected __unused)
{
	// do nothing - console ignores selections
}
