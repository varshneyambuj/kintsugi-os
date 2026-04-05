/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Caz, turok2@currantbun.com
 *       Axel Dörfler, axeld@pinc-software.de
 *       Michael Lotz, mmlr@mlotz.ch
 *       Wim van der Meer, WPJvanderMeer@gmail.com
 *       Joseph Groover, looncraz@looncraz.net
 */


/**
 * @file InterfaceDefs.cpp
 * @brief Global definitions and utility functions for the Interface Kit
 *
 * Implements global Interface Kit functions declared in InterfaceDefs.h,
 * including UI color and font accessors, keyboard modifier queries, modal
 * alert helpers, and workspace management utilities.
 *
 * @see GraphicsDefs.cpp, BScreen
 */


#include <InterfaceDefs.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <Clipboard.h>
#include <ControlLook.h>
#include <Font.h>
#include <Menu.h>
#include <Point.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>

#include <ApplicationPrivate.h>
#include <AppServerLink.h>
#include <ColorConversion.h>
#include <DecorInfo.h>
#include <DefaultColors.h>
#include <DesktopLink.h>
#include <HaikuControlLook.h>
#include <InputServerTypes.h>
#include <input_globals.h>
#include <InterfacePrivate.h>
#include <MenuPrivate.h>
#include <pr_server.h>
#include <ServerProtocol.h>
#include <ServerReadOnlyMemory.h>
#include <truncate_string.h>
#include <utf8_functions.h>
#include <WidthBuffer.h>
#include <WindowInfo.h>


using namespace BPrivate;

/**
 * @brief Legacy BeOS general UI info structure exported for binary compatibility.
 *
 * This structure is exported but never initialized by the system; it exists
 * solely to satisfy old BeOS binaries that reference it.
 */
struct general_ui_info {
	rgb_color	background_color;
	rgb_color	mark_color;
	rgb_color	highlight_color;
	bool		color_frame;
	rgb_color	window_frame_color;
};

/** @brief Global instance of the legacy general UI info structure. */
struct general_ui_info general_info;

/** @brief Global pointer to the active menu_info, set during kit initialization. */
menu_info *_menu_info_ptr_;

/** @brief Sender field name used in notification messages. */
extern "C" const char B_NOTIFICATION_SENDER[] = "be:sender";

/**
 * @brief Default light-theme UI color table, indexed by color_which constant.
 *
 * Provides fallback colors when the app_server shared-memory region is
 * unavailable or when a color entry has not yet been set.
 *
 * @see _kDefaultColorsDark, BPrivate::kDefaultColors, ui_color()
 */
static const rgb_color _kDefaultColors[kColorWhichCount] = {
	{216, 216, 216, 255},	// B_PANEL_BACKGROUND_COLOR
	{216, 216, 216, 255},	// B_MENU_BACKGROUND_COLOR
	{255, 203, 0, 255},		// B_WINDOW_TAB_COLOR
	{0, 0, 229, 255},		// B_KEYBOARD_NAVIGATION_COLOR
	{51, 102, 152, 255},	// B_DESKTOP_COLOR
	{153, 153, 153, 255},	// B_MENU_SELECTED_BACKGROUND_COLOR
	{0, 0, 0, 255},			// B_MENU_ITEM_TEXT_COLOR
	{0, 0, 0, 255},			// B_MENU_SELECTED_ITEM_TEXT_COLOR
	{0, 0, 0, 255},			// B_MENU_SELECTED_BORDER_COLOR
	{0, 0, 0, 255},			// B_PANEL_TEXT_COLOR
	{255, 255, 255, 255},	// B_DOCUMENT_BACKGROUND_COLOR
	{0, 0, 0, 255},			// B_DOCUMENT_TEXT_COLOR
	{222, 222, 222, 255},	// B_CONTROL_BACKGROUND_COLOR
	{0, 0, 0, 255},			// B_CONTROL_TEXT_COLOR
	{172, 172, 172, 255},	// B_CONTROL_BORDER_COLOR
	{102, 152, 203, 255},	// B_CONTROL_HIGHLIGHT_COLOR
	{0, 0, 0, 255},			// B_NAVIGATION_PULSE_COLOR
	{255, 255, 255, 255},	// B_SHINE_COLOR
	{0, 0, 0, 255},			// B_SHADOW_COLOR
	{255, 255, 216, 255},	// B_TOOLTIP_BACKGROUND_COLOR
	{0, 0, 0, 255},			// B_TOOLTIP_TEXT_COLOR
	{0, 0, 0, 255},			// B_WINDOW_TEXT_COLOR
	{232, 232, 232, 255},	// B_WINDOW_INACTIVE_TAB_COLOR
	{80, 80, 80, 255},		// B_WINDOW_INACTIVE_TEXT_COLOR
	{224, 224, 224, 255},	// B_WINDOW_BORDER_COLOR
	{232, 232, 232, 255},	// B_WINDOW_INACTIVE_BORDER_COLOR
	{27, 82, 140, 255},     // B_CONTROL_MARK_COLOR
	{255, 255, 255, 255},	// B_LIST_BACKGROUND_COLOR
	{190, 190, 190, 255},	// B_LIST_SELECTED_BACKGROUND_COLOR
	{0, 0, 0, 255},			// B_LIST_ITEM_TEXT_COLOR
	{0, 0, 0, 255},			// B_LIST_SELECTED_ITEM_TEXT_COLOR
	{216, 216, 216, 255},	// B_SCROLL_BAR_THUMB_COLOR
	{51, 102, 187, 255},	// B_LINK_TEXT_COLOR
	{102, 152, 203, 255},	// B_LINK_HOVER_COLOR
	{145, 112, 155, 255},	// B_LINK_VISITED_COLOR
	{121, 142, 203, 255},	// B_LINK_ACTIVE_COLOR
	{50, 150, 255, 255},	// B_STATUS_BAR_COLOR
	// 100...
	{46, 204, 64, 255},		// B_SUCCESS_COLOR
	{255, 65, 54, 255},		// B_FAILURE_COLOR
	{}
};
/** @brief Public pointer to the default light-theme color table, exposed to BPrivate consumers. */
const rgb_color* BPrivate::kDefaultColors = &_kDefaultColors[0];


/**
 * @brief Default dark-theme UI color table, indexed by color_which constant.
 *
 * Used by BPrivate::GetSystemColor() when @a darkVariant is @c true, providing
 * a complete dark palette that mirrors the structure of @c _kDefaultColors.
 *
 * @see _kDefaultColors, BPrivate::GetSystemColor()
 */
static const rgb_color _kDefaultColorsDark[kColorWhichCount] = {
	{43, 43, 43, 255},		// B_PANEL_BACKGROUND_COLOR
	{28, 28, 28, 255},		// B_MENU_BACKGROUND_COLOR
	{227, 73, 17, 255},		// B_WINDOW_TAB_COLOR
	{0, 0, 229, 255},		// B_KEYBOARD_NAVIGATION_COLOR
	{51, 102, 152, 255},	// B_DESKTOP_COLOR
	{90, 90, 90, 255},		// B_MENU_SELECTED_BACKGROUND_COLOR
	{255, 255, 255, 255},	// B_MENU_ITEM_TEXT_COLOR
	{255, 255, 255, 255},	// B_MENU_SELECTED_ITEM_TEXT_COLOR
	{0, 0, 0, 255},			// B_MENU_SELECTED_BORDER_COLOR
	{253, 253, 253, 255},	// B_PANEL_TEXT_COLOR
	{0, 0, 0, 255},			// B_DOCUMENT_BACKGROUND_COLOR
	{234, 234, 234, 255},	// B_DOCUMENT_TEXT_COLOR
	{29, 29, 29, 255},		// B_CONTROL_BACKGROUND_COLOR
	{230, 230, 230, 255},	// B_CONTROL_TEXT_COLOR
	{195, 195, 195, 255},	// B_CONTROL_BORDER_COLOR
	{75, 124, 168, 255},	// B_CONTROL_HIGHLIGHT_COLOR
	{0, 0, 0, 255},			// B_NAVIGATION_PULSE_COLOR
	{255, 255, 255, 255},	// B_SHINE_COLOR
	{0, 0, 0, 255},			// B_SHADOW_COLOR
	{76, 68, 79, 255},		// B_TOOLTIP_BACKGROUND_COLOR
	{255, 255, 255, 255},	// B_TOOLTIP_TEXT_COLOR
	{255, 255, 255, 255},	// B_WINDOW_TEXT_COLOR
	{203, 32, 9, 255},		// B_WINDOW_INACTIVE_TAB_COLOR
	{255, 255, 255, 255},	// B_WINDOW_INACTIVE_TEXT_COLOR
	{227, 73, 17, 255},		// B_WINDOW_BORDER_COLOR
	{203, 32, 9, 255},		// B_WINDOW_INACTIVE_BORDER_COLOR
	{27, 82, 140, 255},     // B_CONTROL_MARK_COLOR
	{0, 0, 0, 255},			// B_LIST_BACKGROUND_COLOR
	{90, 90, 90, 255},		// B_LIST_SELECTED_BACKGROUND_COLOR
	{255, 255, 255, 255},	// B_LIST_ITEM_TEXT_COLOR
	{255, 255, 255, 255},	// B_LIST_SELECTED_ITEM_TEXT_COLOR
	{39, 39, 39, 255},		// B_SCROLL_BAR_THUMB_COLOR
	{106, 112, 212, 255},	// B_LINK_TEXT_COLOR
	{102, 152, 203, 255},	// B_LINK_HOVER_COLOR
	{145, 112, 155, 255},	// B_LINK_VISITED_COLOR
	{121, 142, 203, 255},	// B_LINK_ACTIVE_COLOR
	{50, 150, 255, 255},	// B_STATUS_BAR_COLOR
	// 100...
	{46, 204, 64, 255},		// B_SUCCESS_COLOR
	{255, 40, 54, 255},		// B_FAILURE_COLOR
	{}
};


/**
 * @brief String name table for each color_which constant.
 *
 * Each entry corresponds positionally to the matching entry in
 * @c _kDefaultColors and is used by ui_color_name() and which_ui_color()
 * to convert between color_which values and their canonical string names.
 *
 * @see ui_color_name(), which_ui_color()
 */
static const char* kColorNames[kColorWhichCount] = {
	"B_PANEL_BACKGROUND_COLOR",
	"B_MENU_BACKGROUND_COLOR",
	"B_WINDOW_TAB_COLOR",
	"B_KEYBOARD_NAVIGATION_COLOR",
	"B_DESKTOP_COLOR",
	"B_MENU_SELECTED_BACKGROUND_COLOR",
	"B_MENU_ITEM_TEXT_COLOR",
	"B_MENU_SELECTED_ITEM_TEXT_COLOR",
	"B_MENU_SELECTED_BORDER_COLOR",
	"B_PANEL_TEXT_COLOR",
	"B_DOCUMENT_BACKGROUND_COLOR",
	"B_DOCUMENT_TEXT_COLOR",
	"B_CONTROL_BACKGROUND_COLOR",
	"B_CONTROL_TEXT_COLOR",
	"B_CONTROL_BORDER_COLOR",
	"B_CONTROL_HIGHLIGHT_COLOR",
	"B_NAVIGATION_PULSE_COLOR",
	"B_SHINE_COLOR",
	"B_SHADOW_COLOR",
	"B_TOOLTIP_BACKGROUND_COLOR",
	"B_TOOLTIP_TEXT_COLOR",
	"B_WINDOW_TEXT_COLOR",
	"B_WINDOW_INACTIVE_TAB_COLOR",
	"B_WINDOW_INACTIVE_TEXT_COLOR",
	"B_WINDOW_BORDER_COLOR",
	"B_WINDOW_INACTIVE_BORDER_COLOR",
	"B_CONTROL_MARK_COLOR",
	"B_LIST_BACKGROUND_COLOR",
	"B_LIST_SELECTED_BACKGROUND_COLOR",
	"B_LIST_ITEM_TEXT_COLOR",
	"B_LIST_SELECTED_ITEM_TEXT_COLOR",
	"B_SCROLL_BAR_THUMB_COLOR",
	"B_LINK_TEXT_COLOR",
	"B_LINK_HOVER_COLOR",
	"B_LINK_VISITED_COLOR",
	"B_LINK_ACTIVE_COLOR",
	"B_STATUS_BAR_COLOR",
	// 100...
	"B_SUCCESS_COLOR",
	"B_FAILURE_COLOR",
	NULL
};

/** @brief Image ID of the loaded ControlLook add-on, or -1 if none is loaded. */
static image_id sControlLookAddon = -1;


namespace BPrivate {


/**
 * @brief Resolves a legacy screen-space mode constant into width, height, and color space.
 *
 * Decodes one of the @c B_8_BIT_640x480 (and similar) mode constants defined
 * in InterfaceDefs.h into its component parameters, suitable for passing to
 * BScreen::SetMode().
 *
 * @param mode       One of the predefined @c B_*_BIT_*x* screen-space constants.
 * @param width      Receives the horizontal pixel count for @a mode.
 * @param height     Receives the vertical pixel count for @a mode.
 * @param colorSpace Receives the color_space value (e.g. @c B_CMAP8, @c B_RGB32)
 *                   corresponding to the bit depth encoded in @a mode.
 *
 * @return @c true if @a mode was recognized and the output parameters were set;
 *         @c false if @a mode is unknown.
 *
 * @see set_screen_space()
 */
bool
get_mode_parameter(uint32 mode, int32& width, int32& height,
	uint32& colorSpace)
{
	switch (mode) {
		case B_8_BIT_640x480:
		case B_8_BIT_800x600:
		case B_8_BIT_1024x768:
		case B_8_BIT_1152x900:
		case B_8_BIT_1280x1024:
		case B_8_BIT_1600x1200:
			colorSpace = B_CMAP8;
			break;

		case B_15_BIT_640x480:
		case B_15_BIT_800x600:
		case B_15_BIT_1024x768:
		case B_15_BIT_1152x900:
		case B_15_BIT_1280x1024:
		case B_15_BIT_1600x1200:
			colorSpace = B_RGB15;
			break;

		case B_16_BIT_640x480:
		case B_16_BIT_800x600:
		case B_16_BIT_1024x768:
		case B_16_BIT_1152x900:
		case B_16_BIT_1280x1024:
		case B_16_BIT_1600x1200:
			colorSpace = B_RGB16;
			break;

		case B_32_BIT_640x480:
		case B_32_BIT_800x600:
		case B_32_BIT_1024x768:
		case B_32_BIT_1152x900:
		case B_32_BIT_1280x1024:
		case B_32_BIT_1600x1200:
			colorSpace = B_RGB32;
			break;

		default:
			return false;
	}

	switch (mode) {
		case B_8_BIT_640x480:
		case B_15_BIT_640x480:
		case B_16_BIT_640x480:
		case B_32_BIT_640x480:
			width = 640; height = 480;
			break;

		case B_8_BIT_800x600:
		case B_15_BIT_800x600:
		case B_16_BIT_800x600:
		case B_32_BIT_800x600:
			width = 800; height = 600;
			break;

		case B_8_BIT_1024x768:
		case B_15_BIT_1024x768:
		case B_16_BIT_1024x768:
		case B_32_BIT_1024x768:
			width = 1024; height = 768;
			break;

		case B_8_BIT_1152x900:
		case B_15_BIT_1152x900:
		case B_16_BIT_1152x900:
		case B_32_BIT_1152x900:
			width = 1152; height = 900;
			break;

		case B_8_BIT_1280x1024:
		case B_15_BIT_1280x1024:
		case B_16_BIT_1280x1024:
		case B_32_BIT_1280x1024:
			width = 1280; height = 1024;
			break;

		case B_8_BIT_1600x1200:
		case B_15_BIT_1600x1200:
		case B_16_BIT_1600x1200:
		case B_32_BIT_1600x1200:
			width = 1600; height = 1200;
			break;
	}

	return true;
}


/**
 * @brief Retrieves the current workspace grid dimensions from the app_server.
 *
 * @param _columns If non-NULL, receives the number of workspace columns.
 * @param _rows    If non-NULL, receives the number of workspace rows.
 *
 * @note Both output parameters fall back to 1 if the server query fails.
 * @see set_workspaces_layout(), count_workspaces()
 */
void
get_workspaces_layout(uint32* _columns, uint32* _rows)
{
	int32 columns = 1;
	int32 rows = 1;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_WORKSPACE_LAYOUT);

	status_t status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<int32>(&columns);
		link.Read<int32>(&rows);
	}

	if (_columns != NULL)
		*_columns = columns;
	if (_rows != NULL)
		*_rows = rows;
}


/**
 * @brief Sets the workspace grid dimensions in the app_server.
 *
 * Requests the server to arrange workspaces in a grid of @a columns columns
 * and @a rows rows. The call is silently ignored when either value is zero.
 *
 * @param columns Number of workspace columns; must be >= 1.
 * @param rows    Number of workspace rows; must be >= 1.
 *
 * @see get_workspaces_layout(), set_workspace_count()
 */
void
set_workspaces_layout(uint32 columns, uint32 rows)
{
	if (columns < 1 || rows < 1)
		return;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_WORKSPACE_LAYOUT);
	link.Attach<int32>(columns);
	link.Attach<int32>(rows);
	link.Flush();
}


}	// namespace BPrivate


/**
 * @brief Enables or disables subpixel antialiasing for font rendering system-wide.
 *
 * @param subpix @c true to enable subpixel antialiasing; @c false to disable.
 *
 * @see get_subpixel_antialiasing(), set_hinting_mode()
 */
void
set_subpixel_antialiasing(bool subpix)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_SUBPIXEL_ANTIALIASING);
	link.Attach<bool>(subpix);
	link.Flush();
}


/**
 * @brief Retrieves the current subpixel antialiasing state from the app_server.
 *
 * @param subpix Receives @c true if subpixel antialiasing is active.
 *
 * @return @c B_OK on success, or a negative error code on failure.
 *
 * @see set_subpixel_antialiasing()
 */
status_t
get_subpixel_antialiasing(bool* subpix)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_SUBPIXEL_ANTIALIASING);
	int32 status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status < B_OK)
		return status;
	link.Read<bool>(subpix);
	return B_OK;
}


/**
 * @brief Sets the global font hinting mode used by the app_server renderer.
 *
 * @param hinting Hinting mode value; valid values are defined in InterfaceDefs.h.
 *
 * @see get_hinting_mode(), set_subpixel_antialiasing()
 */
void
set_hinting_mode(uint8 hinting)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_HINTING);
	link.Attach<uint8>(hinting);
	link.Flush();
}


/**
 * @brief Retrieves the current font hinting mode from the app_server.
 *
 * @param hinting Receives the current hinting mode value.
 *
 * @return @c B_OK on success, or a negative error code on failure.
 *
 * @see set_hinting_mode()
 */
status_t
get_hinting_mode(uint8* hinting)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_HINTING);
	int32 status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status < B_OK)
		return status;
	link.Read<uint8>(hinting);
	return B_OK;
}


/**
 * @brief Sets the average weight used in subpixel antialiasing blending.
 *
 * Controls the contribution of the central sub-pixel channel relative to its
 * neighbors when compositing subpixel-rendered glyphs onto the destination.
 *
 * @param averageWeight Weight value in the range [0, 255].
 *
 * @see get_average_weight(), set_subpixel_antialiasing()
 */
void
set_average_weight(uint8 averageWeight)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_SUBPIXEL_AVERAGE_WEIGHT);
	link.Attach<uint8>(averageWeight);
	link.Flush();
}


/**
 * @brief Retrieves the current subpixel antialiasing average weight from the app_server.
 *
 * @param averageWeight Receives the current weight value.
 *
 * @return @c B_OK on success, or a negative error code on failure.
 *
 * @see set_average_weight()
 */
status_t
get_average_weight(uint8* averageWeight)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_SUBPIXEL_AVERAGE_WEIGHT);
	int32 status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status < B_OK)
		return status;
	link.Read<uint8>(averageWeight);
	return B_OK;
}


/**
 * @brief Sets whether the subpixel order of the display is RGB (regular) or BGR.
 *
 * @param subpixelOrdering @c true for standard RGB subpixel order;
 *                         @c false for reversed BGR order.
 *
 * @see get_is_subpixel_ordering_regular(), set_subpixel_antialiasing()
 */
void
set_is_subpixel_ordering_regular(bool subpixelOrdering)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_SUBPIXEL_ORDERING);
	link.Attach<bool>(subpixelOrdering);
	link.Flush();
}


/**
 * @brief Retrieves the current subpixel ordering setting from the app_server.
 *
 * @param subpixelOrdering Receives @c true if RGB ordering is active.
 *
 * @return @c B_OK on success, or a negative error code on failure.
 *
 * @see set_is_subpixel_ordering_regular()
 */
status_t
get_is_subpixel_ordering_regular(bool* subpixelOrdering)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_SUBPIXEL_ORDERING);
	int32 status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status < B_OK)
		return status;
	link.Read<bool>(subpixelOrdering);
	return B_OK;
}


/**
 * @brief Returns the system color map for the main screen.
 *
 * Delegates to BScreen(B_MAIN_SCREEN_ID).ColorMap(), providing the 8-bit
 * indexed color palette used for @c B_CMAP8 surfaces.
 *
 * @return A pointer to the main screen's color_map, or @c NULL if unavailable.
 *
 * @see BScreen::ColorMap()
 */
const color_map *
system_colors()
{
	return BScreen(B_MAIN_SCREEN_ID).ColorMap();
}


/**
 * @brief Sets the screen resolution and color depth using a legacy space constant.
 *
 * Decodes @a space into width, height, and color depth, then applies them to
 * the display identified by @a index via BScreen::SetMode(). The refresh rate
 * is preserved from the current mode.
 *
 * @param index Workspace index identifying the target display configuration.
 * @param space One of the @c B_*_BIT_*x* constants defined in InterfaceDefs.h.
 * @param stick  If @c true, the mode change persists across reboots.
 *
 * @return @c B_OK on success, @c B_BAD_VALUE if @a space is not a recognized
 *         constant, or another error code propagated from BScreen::SetMode().
 *
 * @see get_mode_parameter(), BScreen::SetMode()
 */
status_t
set_screen_space(int32 index, uint32 space, bool stick)
{
	int32 width;
	int32 height;
	uint32 depth;
	if (!BPrivate::get_mode_parameter(space, width, height, depth))
		return B_BAD_VALUE;

	BScreen screen(B_MAIN_SCREEN_ID);
	display_mode mode;

	// TODO: What about refresh rate ?
	// currently we get it from the current video mode, but
	// this might be not so wise.
	status_t status = screen.GetMode(index, &mode);
	if (status < B_OK)
		return status;

	mode.virtual_width = width;
	mode.virtual_height = height;
	mode.space = depth;

	return screen.SetMode(index, &mode, stick);
}


/**
 * @brief Retrieves the global scroll bar appearance and behavior settings.
 *
 * Queries the app_server for the current scroll_bar_info, which controls
 * proportional indicator display, thumb visibility, arrow placement, and
 * scroll repeat delay.
 *
 * @param info Pointer to a scroll_bar_info struct to fill; must not be NULL.
 *
 * @return @c B_OK on success, @c B_BAD_VALUE if @a info is NULL, or
 *         @c B_ERROR if the server request fails.
 *
 * @see set_scroll_bar_info(), BScrollBar
 */
status_t
get_scroll_bar_info(scroll_bar_info *info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_SCROLLBAR_INFO);

	int32 code;
	if (link.FlushWithReply(code) == B_OK
		&& code == B_OK) {
		link.Read<scroll_bar_info>(info);
		return B_OK;
	}

	return B_ERROR;
}


/**
 * @brief Applies new global scroll bar appearance and behavior settings.
 *
 * Sends the supplied scroll_bar_info to the app_server, which applies the
 * settings to all subsequently drawn scroll bars.
 *
 * @param info Pointer to a scroll_bar_info struct containing the new settings;
 *             must not be NULL.
 *
 * @return @c B_OK on success, @c B_BAD_VALUE if @a info is NULL, or
 *         @c B_ERROR if the server request fails.
 *
 * @see get_scroll_bar_info(), BScrollBar
 */
status_t
set_scroll_bar_info(scroll_bar_info *info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	int32 code;

	link.StartMessage(AS_SET_SCROLLBAR_INFO);
	link.Attach<scroll_bar_info>(*info);

	if (link.FlushWithReply(code) == B_OK
		&& code == B_OK)
		return B_OK;

	return B_ERROR;
}


/**
 * @brief Retrieves the button count of the default (primary) mouse.
 *
 * @param type Receives the mouse type (number of buttons).
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_mouse_type(int32), get_mouse_type(const char*, int32*)
 */
status_t
get_mouse_type(int32 *type)
{
	BMessage command(IS_GET_MOUSE_TYPE);
	BMessage reply;

	status_t err = _control_input_server_(&command, &reply);
	if (err != B_OK)
		return err;
	return reply.FindInt32("mouse_type", type);
}


/**
 * @brief Sets the button count of the default (primary) mouse.
 *
 * @param type The desired mouse type (number of buttons).
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_type(int32*), set_mouse_type(const char*, int32)
 */
status_t
set_mouse_type(int32 type)
{
	BMessage command(IS_SET_MOUSE_TYPE);
	BMessage reply;

	status_t err = command.AddInt32("mouse_type", type);
	if (err != B_OK)
		return err;
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the button count of a named mouse device.
 *
 * @param mouse_name  Input-server device name of the mouse to query.
 * @param type        Receives the mouse type (number of buttons).
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_type(int32*), set_mouse_type(const char*, int32)
 */
status_t
get_mouse_type(const char* mouse_name, int32 *type)
{
	BMessage command(IS_GET_MOUSE_TYPE);
	BMessage reply;
	command.AddString("mouse_name", mouse_name);

	status_t err = _control_input_server_(&command, &reply);
	if (err != B_OK)
		return err;

	return reply.FindInt32("mouse_type", type);
}


/**
 * @brief Sets the button count of a named mouse device.
 *
 * @param mouse_name  Input-server device name of the mouse to configure.
 * @param type        The desired mouse type (number of buttons).
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_type(const char*, int32*), set_mouse_type(int32)
 */
status_t
set_mouse_type(const char* mouse_name, int32 type)
{
	BMessage command(IS_SET_MOUSE_TYPE);
	BMessage reply;

	status_t err_mouse_name = command.AddString("mouse_name", mouse_name);
	if (err_mouse_name != B_OK)
		return err_mouse_name;

	status_t err = command.AddInt32("mouse_type", type);
	if (err != B_OK)
		return err;
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the button mapping of the default (primary) mouse.
 *
 * Delegates to get_mouse_map(const char*, mouse_map*) with an empty device name.
 *
 * @param map Receives the current mouse button mapping.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_map(const char*, mouse_map*), set_mouse_map(mouse_map*)
 */
status_t
get_mouse_map(mouse_map* map)
{
	return get_mouse_map("", map);
}


/**
 * @brief Sets the button mapping of the default (primary) mouse.
 *
 * Delegates to set_mouse_map(const char*, mouse_map*) with an empty device name.
 *
 * @param map Pointer to a mouse_map struct containing the desired button assignments.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_mouse_map(const char*, mouse_map*), get_mouse_map(mouse_map*)
 */
status_t
set_mouse_map(mouse_map* map)
{
	return set_mouse_map("", map);
}


/**
 * @brief Retrieves the button mapping of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to query.
 * @param map        Receives the current button mapping for the named device.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_mouse_map(const char*, mouse_map*), get_mouse_map(mouse_map*)
 */
status_t
get_mouse_map(const char* mouse_name, mouse_map* map)
{
	BMessage command(IS_GET_MOUSE_MAP);
	BMessage reply;
	const void *data = 0;
	ssize_t count;

	status_t err = command.AddString("mouse_name", mouse_name);
	if (err == B_OK)
		err = _control_input_server_(&command, &reply);
	if (err == B_OK)
		err = reply.FindData("mousemap", B_RAW_TYPE, &data, &count);
	if (err == B_OK)
		memcpy(map, data, count);

	return err;
}


/**
 * @brief Sets the button mapping of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to configure.
 * @param map        Pointer to a mouse_map struct with the desired button assignments.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_map(const char*, mouse_map*), set_mouse_map(mouse_map*)
 */
status_t
set_mouse_map(const char* mouse_name, mouse_map* map)
{
	BMessage command(IS_SET_MOUSE_MAP);
	BMessage reply;

	status_t err = command.AddString("mouse_name", mouse_name);
	if (err == B_OK)
		err = command.AddData("mousemap", B_RAW_TYPE, map, sizeof(mouse_map));
	if (err != B_OK)
		return err;
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the double-click speed of the default (primary) mouse.
 *
 * Delegates to get_click_speed(const char*, bigtime_t*) with an empty device name.
 *
 * @param speed Receives the maximum interval between clicks (in microseconds)
 *              that counts as a double-click. Defaults to 500 000 µs if unset.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_click_speed(const char*, bigtime_t*), set_click_speed(bigtime_t)
 */
status_t
get_click_speed(bigtime_t* speed)
{
	return get_click_speed("", speed);
}


/**
 * @brief Sets the double-click speed of the default (primary) mouse.
 *
 * Delegates to set_click_speed(const char*, bigtime_t) with an empty device name.
 *
 * @param speed Maximum interval between clicks (in microseconds) for a
 *              double-click to be recognized.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_click_speed(const char*, bigtime_t), get_click_speed(bigtime_t*)
 */
status_t
set_click_speed(bigtime_t speed)
{
	return set_click_speed("", speed);
}


/**
 * @brief Retrieves the double-click speed of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to query.
 * @param speed      Receives the maximum double-click interval in microseconds.
 *                   Set to 500 000 µs if the server does not report a value.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_click_speed(const char*, bigtime_t), get_click_speed(bigtime_t*)
 */
status_t
get_click_speed(const char* mouse_name, bigtime_t* speed)
{
	BMessage command(IS_GET_CLICK_SPEED);
	BMessage reply;

	status_t err = command.AddString("mouse_name", mouse_name);
	if (err == B_OK)
		err = _control_input_server_(&command, &reply);
	if (err != B_OK)
		return err;

	if (reply.FindInt64("speed", speed) != B_OK)
		*speed = 500000;

	return B_OK;
}


/**
 * @brief Sets the double-click speed of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to configure.
 * @param speed      Maximum double-click interval in microseconds.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_click_speed(const char*, bigtime_t*), set_click_speed(bigtime_t)
 */
status_t
set_click_speed(const char* mouse_name, bigtime_t speed)
{
	BMessage command(IS_SET_CLICK_SPEED);
	BMessage reply;

	status_t err = command.AddString("mouse_name", mouse_name);
	if (err == B_OK)
		err = command.AddInt64("speed", speed);
	if (err != B_OK)
		return err;
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the pointer speed of the default (primary) mouse.
 *
 * @param speed Receives the speed value; defaults to 65536 if not set.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_mouse_speed(int32), get_mouse_speed(const char*, int32*)
 */
status_t
get_mouse_speed(int32 *speed)
{
	BMessage command(IS_GET_MOUSE_SPEED);
	BMessage reply;

	status_t err = _control_input_server_(&command, &reply);
	if (err != B_OK)
		return err;

	if (reply.FindInt32("speed", speed) != B_OK)
		*speed = 65536;

	return B_OK;
}


/**
 * @brief Sets the pointer speed of the default (primary) mouse.
 *
 * @param speed Desired pointer speed value.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_speed(int32*), set_mouse_speed(const char*, int32)
 */
status_t
set_mouse_speed(int32 speed)
{
	BMessage command(IS_SET_MOUSE_SPEED);
	BMessage reply;
	command.AddInt32("speed", speed);
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the pointer speed of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to query.
 * @param speed      Receives the current pointer speed.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_mouse_speed(const char*, int32), get_mouse_speed(int32*)
 */
status_t
get_mouse_speed(const char* mouse_name, int32 *speed)
{
	BMessage command(IS_GET_MOUSE_SPEED);
	BMessage reply;
	command.AddString("mouse_name", mouse_name);

	status_t err = _control_input_server_(&command, &reply);
	if (err != B_OK)
		return err;

	err = reply.FindInt32("speed", speed);
	if (err != B_OK)
		return err;

	return B_OK;
}


/**
 * @brief Sets the pointer speed of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to configure.
 * @param speed      The desired pointer speed value.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_speed(const char*, int32*), set_mouse_speed(int32)
 */
status_t
set_mouse_speed(const char* mouse_name, int32 speed)
{
	BMessage command(IS_SET_MOUSE_SPEED);
	BMessage reply;
	command.AddString("mouse_name", mouse_name);

	command.AddInt32("speed", speed);

	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the pointer acceleration of the default (primary) mouse.
 *
 * @param speed Receives the acceleration value; defaults to 65536 if not set.
 *
 * @return Always returns @c B_OK.
 *
 * @see set_mouse_acceleration(int32), get_mouse_acceleration(const char*, int32*)
 */
status_t
get_mouse_acceleration(int32 *speed)
{
	BMessage command(IS_GET_MOUSE_ACCELERATION);
	BMessage reply;

	_control_input_server_(&command, &reply);

	if (reply.FindInt32("speed", speed) != B_OK)
		*speed = 65536;

	return B_OK;
}


/**
 * @brief Sets the pointer acceleration of the default (primary) mouse.
 *
 * @param speed Desired acceleration value.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_acceleration(int32*), set_mouse_acceleration(const char*, int32)
 */
status_t
set_mouse_acceleration(int32 speed)
{
	BMessage command(IS_SET_MOUSE_ACCELERATION);
	BMessage reply;
	command.AddInt32("speed", speed);
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the pointer acceleration of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to query.
 * @param speed      Receives the acceleration value; defaults to 65536 if not set.
 *
 * @return Always returns @c B_OK.
 *
 * @see set_mouse_acceleration(const char*, int32), get_mouse_acceleration(int32*)
 */
status_t
get_mouse_acceleration(const char* mouse_name, int32 *speed)
{
	BMessage command(IS_GET_MOUSE_ACCELERATION);
	BMessage reply;
	command.AddString("mouse_name", mouse_name);

	_control_input_server_(&command, &reply);

	if (reply.FindInt32("speed", speed) != B_OK)
		*speed = 65536;

	return B_OK;
}


/**
 * @brief Sets the pointer acceleration of a named mouse device.
 *
 * @param mouse_name Input-server device name of the mouse to configure.
 * @param speed      The desired acceleration value.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_mouse_acceleration(const char*, int32*), set_mouse_acceleration(int32)
 */
status_t
set_mouse_acceleration(const char* mouse_name, int32 speed)
{
	BMessage command(IS_SET_MOUSE_ACCELERATION);
	BMessage reply;
	command.AddString("mouse_name", mouse_name);

	command.AddInt32("speed", speed);

	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the key auto-repeat rate from the input server.
 *
 * @param rate Receives the repeat rate; falls back to 250 000 if not available.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_key_repeat_rate(), get_key_repeat_delay()
 */
status_t
get_key_repeat_rate(int32 *rate)
{
	BMessage command(IS_GET_KEY_REPEAT_RATE);
	BMessage reply;

	status_t err = _control_input_server_(&command, &reply);

	if (err == B_OK)
		err = reply.FindInt32("rate", rate);

	if (err != B_OK) {
		*rate = 250000;
		return err;
	}

	return B_OK;
}


/**
 * @brief Sets the key auto-repeat rate in the input server.
 *
 * @param rate Desired repeat rate value.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_key_repeat_rate(), set_key_repeat_delay()
 */
status_t
set_key_repeat_rate(int32 rate)
{
	BMessage command(IS_SET_KEY_REPEAT_RATE);
	BMessage reply;
	command.AddInt32("rate", rate);
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Retrieves the initial key auto-repeat delay from the input server.
 *
 * @param delay Receives the delay in microseconds; falls back to 200 µs if not set.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see set_key_repeat_delay(), get_key_repeat_rate()
 */
status_t
get_key_repeat_delay(bigtime_t *delay)
{
	BMessage command(IS_GET_KEY_REPEAT_DELAY);
	BMessage reply;

	status_t err = _control_input_server_(&command, &reply);

	if (err == B_OK)
		err = reply.FindInt64("delay", delay);

	if (err != B_OK) {
		*delay = 200;
		return err;
	}

	return B_OK;
}


/**
 * @brief Sets the initial key auto-repeat delay in the input server.
 *
 * @param delay Desired delay before auto-repeat begins, in microseconds.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_key_repeat_delay(), set_key_repeat_rate()
 */
status_t
set_key_repeat_delay(bigtime_t  delay)
{
	BMessage command(IS_SET_KEY_REPEAT_DELAY);
	BMessage reply;
	command.AddInt64("delay", delay);
	return _control_input_server_(&command, &reply);
}


/**
 * @brief Returns the currently active keyboard modifier key bitmask.
 *
 * Queries the input server for the real-time modifier state, combining flags
 * such as @c B_SHIFT_KEY, @c B_CONTROL_KEY, @c B_COMMAND_KEY, etc.
 *
 * @return A bitmask of the currently held modifier keys, or 0 if the query fails.
 *
 * @see get_modifier_key(), get_key_info()
 */
uint32
modifiers()
{
	BMessage command(IS_GET_MODIFIERS);
	BMessage reply;
	int32 err, modifier;

	_control_input_server_(&command, &reply);

	if (reply.FindInt32("status", &err) != B_OK)
		return 0;

	if (reply.FindInt32("modifiers", &modifier) != B_OK)
		return 0;

	return modifier;
}


/**
 * @brief Retrieves a snapshot of the current keyboard state.
 *
 * Fills @a info with the current modifier mask and key_states bitmap obtained
 * from the input server.
 *
 * @param info Pointer to a key_info struct to receive the keyboard snapshot.
 *
 * @return @c B_OK on success, or @c B_ERROR if the query fails.
 *
 * @see modifiers(), get_key_map()
 */
status_t
get_key_info(key_info *info)
{
	BMessage command(IS_GET_KEY_INFO);
	BMessage reply;
	const void *data = 0;
	int32 err;
	ssize_t count;

	_control_input_server_(&command, &reply);

	if (reply.FindInt32("status", &err) != B_OK)
		return B_ERROR;

	if (reply.FindData("key_info", B_ANY_TYPE, &data, &count) != B_OK)
		return B_ERROR;

	memcpy(info, data, count);
	return B_OK;
}


/**
 * @brief Retrieves the current system key map and its associated character table.
 *
 * Allocates and returns heap copies of both the key_map structure and the
 * accompanying UTF-8 character buffer. The caller is responsible for freeing
 * both pointers with free().
 *
 * @param map        Receives a newly allocated key_map; set to NULL on failure.
 * @param key_buffer Receives a newly allocated character buffer; set to NULL on failure.
 *
 * @see _get_key_map(), set_key_map() (not in this file), get_modifier_key()
 */
void
get_key_map(key_map **map, char **key_buffer)
{
	_get_key_map(map, key_buffer, NULL);
}


/**
 * @brief Internal implementation of get_key_map() that also returns the buffer size.
 *
 * Queries the input server for the key map and character buffer, allocates
 * heap copies of each, and optionally reports the buffer byte count.
 *
 * @param map             Receives a newly allocated key_map; set to NULL on failure.
 * @param key_buffer      Receives a newly allocated character buffer; NULL on failure.
 * @param key_buffer_size If non-NULL, receives the size of @a key_buffer in bytes.
 *
 * @see get_key_map()
 */
void
_get_key_map(key_map **map, char **key_buffer, ssize_t *key_buffer_size)
{
	BMessage command(IS_GET_KEY_MAP);
	BMessage reply;
	ssize_t map_count, key_count;
	const void *map_array = 0, *key_array = 0;
	if (key_buffer_size == NULL)
		key_buffer_size = &key_count;

	_control_input_server_(&command, &reply);

	if (reply.FindData("keymap", B_ANY_TYPE, &map_array, &map_count) != B_OK) {
		*map = 0; *key_buffer = 0;
		return;
	}

	if (reply.FindData("key_buffer", B_ANY_TYPE, &key_array, key_buffer_size)
			!= B_OK) {
		*map = 0; *key_buffer = 0;
		return;
	}

	*map = (key_map *)malloc(map_count);
	memcpy(*map, map_array, map_count);
	*key_buffer = (char *)malloc(*key_buffer_size);
	memcpy(*key_buffer, key_array, *key_buffer_size);
}


/**
 * @brief Retrieves the hardware keyboard ID from the input server.
 *
 * @param id Receives the 16-bit keyboard identifier reported by the device.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_key_map(), modifiers()
 */
status_t
get_keyboard_id(uint16 *id)
{
	BMessage command(IS_GET_KEYBOARD_ID);
	BMessage reply;
	uint16 kid;

	_control_input_server_(&command, &reply);

	status_t err = reply.FindInt16("id", (int16 *)&kid);
	if (err != B_OK)
		return err;
	*id = kid;

	return B_OK;
}


/**
 * @brief Returns the scan code assigned to a given modifier role.
 *
 * Queries the input server for the physical key currently mapped to the
 * specified modifier (e.g. @c B_LEFT_SHIFT_KEY, @c B_CAPS_LOCK).
 *
 * @param modifier One of the modifier-role constants from InterfaceDefs.h.
 * @param key      Receives the corresponding scan code.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see modifiers(), get_key_map(), set_modifier_key()
 */
status_t
get_modifier_key(uint32 modifier, uint32 *key)
{
	BMessage command(IS_GET_MODIFIER_KEY);
	BMessage reply;
	uint32 rkey;

	command.AddInt32("modifier", modifier);
	_control_input_server_(&command, &reply);

	status_t err = reply.FindInt32("key", (int32 *) &rkey);
	if (err != B_OK)
		return err;
	*key = rkey;

	return B_OK;
}


/**
 * @brief Assigns a scan code to a given modifier role.
 *
 * Instructs the input server to treat @a key as the physical key for the
 * specified modifier role.
 *
 * @param modifier One of the modifier-role constants from InterfaceDefs.h.
 * @param key      Scan code of the physical key to assign.
 *
 * @see get_modifier_key(), set_keyboard_locks()
 */
void
set_modifier_key(uint32 modifier, uint32 key)
{
	BMessage command(IS_SET_MODIFIER_KEY);
	BMessage reply;

	command.AddInt32("modifier", modifier);
	command.AddInt32("key", key);
	_control_input_server_(&command, &reply);
}


/**
 * @brief Sets the active keyboard lock states (Caps Lock, Num Lock, Scroll Lock).
 *
 * Sends the desired lock-state bitmask to the input server, which updates the
 * corresponding LED indicators and modifier flags accordingly.
 *
 * @param modifiers Bitmask of lock constants (e.g. @c B_CAPS_LOCK, @c B_NUM_LOCK)
 *                  to activate; clear bits disable the corresponding lock.
 *
 * @see modifiers(), get_modifier_key()
 */
void
set_keyboard_locks(uint32 modifiers)
{
	BMessage command(IS_SET_KEYBOARD_LOCKS);
	BMessage reply;

	command.AddInt32("locks", modifiers);
	_control_input_server_(&command, &reply);
}


/**
 * @brief Restores the key map to the default settings stored on disk.
 *
 * Sends an @c IS_RESTORE_KEY_MAP message to the input server, which reloads
 * the key map from the user's or system-wide key map preference file.
 *
 * @return @c B_OK on success, or an error code from the input server.
 *
 * @see get_key_map(), _get_key_map()
 */
status_t
_restore_key_map_()
{
	BMessage message(IS_RESTORE_KEY_MAP);
	BMessage reply;

	return _control_input_server_(&message, &reply);
}


/**
 * @brief Returns the current keyboard navigation highlight color.
 *
 * Queries the app_server for the @c B_KEYBOARD_NAVIGATION_COLOR system color,
 * which is used to draw focus rings on keyboard-navigable controls.
 *
 * @return The current keyboard navigation color as an rgb_color.
 *
 * @see ui_color(), B_KEYBOARD_NAVIGATION_COLOR
 */
rgb_color
keyboard_navigation_color()
{
	// Queries the app_server
	return ui_color(B_KEYBOARD_NAVIGATION_COLOR);
}


/**
 * @brief Returns the total number of workspaces in the current grid.
 *
 * Computes the product of the column and row counts reported by
 * get_workspaces_layout().
 *
 * @return Total workspace count (columns * rows).
 *
 * @see get_workspaces_layout(), set_workspace_count()
 */
int32
count_workspaces()
{
	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	return columns * rows;
}


/**
 * @brief Sets the total number of workspaces by choosing a near-square grid layout.
 *
 * Finds the largest factor of @a count that is at most its square root to use
 * as the row count, then derives columns as @a count divided by rows, and
 * applies the result via set_workspaces_layout().
 *
 * @param count Desired total number of workspaces.
 *
 * @see set_workspaces_layout(), count_workspaces()
 */
void
set_workspace_count(int32 count)
{
	int32 squareRoot = (int32)sqrt(count);

	int32 rows = 1;
	for (int32 i = 2; i <= squareRoot; i++) {
		if (count % i == 0)
			rows = i;
	}

	int32 columns = count / rows;

	BPrivate::set_workspaces_layout(columns, rows);
}


/**
 * @brief Returns the zero-based index of the currently active workspace.
 *
 * Queries the app_server for the index of the workspace that is currently
 * displayed on screen.
 *
 * @return Zero-based workspace index, or 0 if the query fails.
 *
 * @see activate_workspace(), count_workspaces()
 */
int32
current_workspace()
{
	int32 index = 0;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_CURRENT_WORKSPACE);

	int32 status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		link.Read<int32>(&index);

	return index;
}


/**
 * @brief Switches the display to the specified workspace.
 *
 * Sends an @c AS_ACTIVATE_WORKSPACE message to the app_server requesting that
 * the given workspace become the active one. The transition is not animated.
 *
 * @param workspace Zero-based index of the workspace to activate.
 *
 * @see current_workspace(), count_workspaces()
 */
void
activate_workspace(int32 workspace)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_ACTIVATE_WORKSPACE);
	link.Attach<int32>(workspace);
	link.Attach<bool>(false);
	link.Flush();
}


/**
 * @brief Returns the time elapsed since the last user input event.
 *
 * Queries the app_server for the duration since the most recent keyboard or
 * mouse event was received, which can be used to implement screensavers or
 * power-management policies.
 *
 * @return Idle time in microseconds, or 0 if the query fails.
 *
 * @see current_workspace()
 */
bigtime_t
idle_time()
{
	bigtime_t idletime = 0;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_IDLE_TIME);

	int32 code;
	if (link.FlushWithReply(code) == B_OK && code == B_OK)
		link.Read<int64>(&idletime);

	return idletime;
}


/**
 * @brief Launches the system Printer preferences application.
 *
 * Uses be_roster to launch the application registered for the
 * @c PRNT_SIGNATURE_TYPE MIME type. Does nothing if @c be_roster is NULL.
 *
 * @see run_add_printer_panel(), BRoster::Launch()
 */
void
run_select_printer_panel()
{
	if (be_roster == NULL)
		return;

	// Launches the Printer prefs app via the Roster
	be_roster->Launch(PRNT_SIGNATURE_TYPE);
}


/**
 * @brief Launches the Printer preferences application and requests the Add Printer panel.
 *
 * Calls run_select_printer_panel() to ensure the Printer prefs app is running,
 * then sends it a @c PRINTERS_ADD_PRINTER message to open the add-printer dialog.
 *
 * @see run_select_printer_panel(), run_be_about()
 */
void
run_add_printer_panel()
{
	// Launches the Printer prefs app via the Roster and asks it to
	// add a printer
	run_select_printer_panel();

	BMessenger printerPanelMessenger(PRNT_SIGNATURE_TYPE);
	printerPanelMessenger.SendMessage(PRINTERS_ADD_PRINTER);
}


/**
 * @brief Launches the system About application.
 *
 * Uses be_roster to launch the application registered for the
 * @c "application/x-vnd.Haiku-About" MIME type. Does nothing if
 * @c be_roster is NULL.
 *
 * @see run_add_printer_panel(), BRoster::Launch()
 */
void
run_be_about()
{
	if (be_roster != NULL)
		be_roster->Launch("application/x-vnd.Haiku-About");
}


/**
 * @brief Enables or disables the deprecated focus-follows-mouse mode.
 *
 * This is a compatibility wrapper; @c true maps to @c B_FOCUS_FOLLOWS_MOUSE
 * and @c false maps to @c B_NORMAL_MOUSE via set_mouse_mode().
 *
 * @param follow @c true to enable focus-follows-mouse; @c false to restore
 *               normal click-to-focus behavior.
 *
 * @note This API is deprecated. Prefer set_mouse_mode() with an explicit
 *       mode_mouse constant.
 * @see set_mouse_mode(), focus_follows_mouse()
 */
void
set_focus_follows_mouse(bool follow)
{
	// obviously deprecated API
	set_mouse_mode(follow ? B_FOCUS_FOLLOWS_MOUSE : B_NORMAL_MOUSE);
}


/**
 * @brief Returns whether focus-follows-mouse mode is currently active.
 *
 * This is a compatibility wrapper that checks whether mouse_mode() returns
 * @c B_FOCUS_FOLLOWS_MOUSE.
 *
 * @return @c true if the current mouse mode is focus-follows-mouse.
 *
 * @note This API is deprecated. Prefer mouse_mode() for finer-grained queries.
 * @see mouse_mode(), set_focus_follows_mouse()
 */
bool
focus_follows_mouse()
{
	return mouse_mode() == B_FOCUS_FOLLOWS_MOUSE;
}


/**
 * @brief Sets the global mouse focus mode in the app_server.
 *
 * Controls whether a window must be clicked to receive focus
 * (@c B_NORMAL_MOUSE), or whether merely hovering is sufficient
 * (@c B_FOCUS_FOLLOWS_MOUSE, @c B_WARP_MOUSE, @c B_INSTANT_WARP_MOUSE).
 *
 * @param mode The desired mode_mouse constant.
 *
 * @see mouse_mode(), set_focus_follows_mouse(), set_focus_follows_mouse_mode()
 */
void
set_mouse_mode(mode_mouse mode)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_MOUSE_MODE);
	link.Attach<mode_mouse>(mode);
	link.Flush();
}


/**
 * @brief Returns the current global mouse focus mode from the app_server.
 *
 * Queries the server for the active mode_mouse value, which determines whether
 * windows require a click for focus or whether hovering is sufficient.
 *
 * @return The active mode_mouse constant; defaults to @c B_NORMAL_MOUSE if
 *         the query fails.
 *
 * @see set_mouse_mode(), focus_follows_mouse(), focus_follows_mouse_mode()
 */
mode_mouse
mouse_mode()
{
	// Gets the mouse focus style, such as activate to click,
	// focus to click, ...
	mode_mouse mode = B_NORMAL_MOUSE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_MOUSE_MODE);

	int32 code;
	if (link.FlushWithReply(code) == B_OK && code == B_OK)
		link.Read<mode_mouse>(&mode);

	return mode;
}


/**
 * @brief Sets the focus-follows-mouse sub-mode in the app_server.
 *
 * Allows fine-grained control over how focus is transferred when
 * focus-follows-mouse is active, for example whether the window is also
 * raised (@c B_NORMAL_FOCUS_FOLLOWS_MOUSE vs. @c B_STRICT_FOCUS_FOLLOWS_MOUSE).
 *
 * @param mode The desired mode_focus_follows_mouse constant.
 *
 * @see focus_follows_mouse_mode(), set_mouse_mode()
 */
void
set_focus_follows_mouse_mode(mode_focus_follows_mouse mode)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_FOCUS_FOLLOWS_MOUSE_MODE);
	link.Attach<mode_focus_follows_mouse>(mode);
	link.Flush();
}


/**
 * @brief Returns the current focus-follows-mouse sub-mode from the app_server.
 *
 * @return The active mode_focus_follows_mouse constant; defaults to
 *         @c B_NORMAL_FOCUS_FOLLOWS_MOUSE if the query fails.
 *
 * @see set_focus_follows_mouse_mode(), mouse_mode()
 */
mode_focus_follows_mouse
focus_follows_mouse_mode()
{
	mode_focus_follows_mouse mode = B_NORMAL_FOCUS_FOLLOWS_MOUSE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_FOCUS_FOLLOWS_MOUSE_MODE);

	int32 code;
	if (link.FlushWithReply(code) == B_OK && code == B_OK)
		link.Read<mode_focus_follows_mouse>(&mode);

	return mode;
}


/**
 * @brief Returns the current cursor position and pressed button mask.
 *
 * Queries the app_server for the real-time mouse state. At least one of
 * @a screenWhere and @a buttons must be non-NULL.
 *
 * @param screenWhere If non-NULL, receives the cursor position in screen coordinates.
 * @param buttons     If non-NULL, receives a bitmask of currently pressed buttons.
 *
 * @return @c B_OK on success, @c B_BAD_VALUE if both pointers are NULL, or
 *         another error code if the server request fails.
 *
 * @see get_mouse_bitmap(), BView::GetMouse()
 */
status_t
get_mouse(BPoint* screenWhere, uint32* buttons)
{
	if (screenWhere == NULL && buttons == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_CURSOR_POSITION);

	int32 code;
	status_t ret = link.FlushWithReply(code);
	if (ret != B_OK)
		return ret;
	if (code != B_OK)
		return code;

	if (screenWhere != NULL)
		ret = link.Read<BPoint>(screenWhere);
	else {
		BPoint dummy;
		ret = link.Read<BPoint>(&dummy);
	}
	if (ret != B_OK)
		return ret;

	if (buttons != NULL)
		ret = link.Read<uint32>(buttons);
	else {
		uint32 dummy;
		ret = link.Read<uint32>(&dummy);
	}

	return ret;
}


/**
 * @brief Retrieves the current cursor image and its hot-spot position.
 *
 * Allocates a new BBitmap containing a copy of the current hardware cursor
 * image. The caller takes ownership of the returned bitmap and must delete it.
 *
 * @param bitmap  If non-NULL, receives a newly allocated BBitmap with the
 *                cursor pixels in @c B_RGBA32 format.
 * @param hotspot If non-NULL, receives the cursor's hot-spot in bitmap-local
 *                coordinates.
 *
 * @return @c B_OK on success, @c B_BAD_VALUE if both pointers are NULL,
 *         @c B_NO_MEMORY on allocation failure, or another error code on
 *         server communication failure.
 *
 * @see get_mouse(), BCursor
 */
status_t
get_mouse_bitmap(BBitmap** bitmap, BPoint* hotspot)
{
	if (bitmap == NULL && hotspot == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_CURSOR_BITMAP);

	int32 code;
	status_t status = link.FlushWithReply(code);
	if (status != B_OK)
		return status;
	if (code != B_OK)
		return code;

	uint32 size = 0;
	uint32 cursorWidth = 0;
	uint32 cursorHeight = 0;
	color_space colorspace = B_RGBA32;

	// if link.Read() returns an error, the same error will be returned on
	// subsequent calls, so we'll check only the return value of the last call
	link.Read<uint32>(&size);
	link.Read<uint32>(&cursorWidth);
	link.Read<uint32>(&cursorHeight);
	link.Read<color_space>(&colorspace);
	if (hotspot == NULL) {
		BPoint dummy;
		link.Read<BPoint>(&dummy);
	} else
		link.Read<BPoint>(hotspot);

	void* data = NULL;
	if (size > 0)
		data = malloc(size);
	if (data == NULL)
		return B_NO_MEMORY;

	status = link.Read(data, size);
	if (status != B_OK) {
		free(data);
		return status;
	}

	BBitmap* cursorBitmap = new (std::nothrow) BBitmap(BRect(0, 0,
		cursorWidth - 1, cursorHeight - 1), colorspace);

	if (cursorBitmap == NULL) {
		free(data);
		return B_NO_MEMORY;
	}
	status = cursorBitmap->InitCheck();
	if (status == B_OK)
		cursorBitmap->SetBits(data, size, 0, colorspace);

	free(data);

	if (status == B_OK && bitmap != NULL)
		*bitmap = cursorBitmap;
	else
		delete cursorBitmap;

	return status;
}


/**
 * @brief Sets whether the first click on an inactive window activates it and
 *        is also delivered to the view as a regular mouse event.
 *
 * @param acceptFirstClick @c true to deliver the activating click to the view;
 *                         @c false to consume it as an activation-only event.
 *
 * @see accept_first_click(), set_mouse_mode()
 */
void
set_accept_first_click(bool acceptFirstClick)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_ACCEPT_FIRST_CLICK);
	link.Attach<bool>(acceptFirstClick);
	link.Flush();
}


/**
 * @brief Returns whether the first click on an inactive window is passed to the view.
 *
 * @return @c true if accept-first-click is enabled; defaults to @c true if the
 *         server query fails.
 *
 * @see set_accept_first_click()
 */
bool
accept_first_click()
{
	// Gets the accept first click status
	bool acceptFirstClick = true;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_ACCEPT_FIRST_CLICK);

	int32 code;
	if (link.FlushWithReply(code) == B_OK && code == B_OK)
		link.Read<bool>(&acceptFirstClick);

	return acceptFirstClick;
}


/**
 * @brief Returns the current value of a named system UI color.
 *
 * When a BApplication with a valid server connection exists, the color is
 * read from the shared app_server memory region. If the entry in that region
 * is @c B_TRANSPARENT_COLOR (unset), or when no application is running, the
 * built-in light-theme default for @a which is returned instead.
 *
 * @param which A color_which constant identifying the desired system color.
 *
 * @return The rgb_color for @a which, or black (0, 0, 0) if @a which is unknown.
 *
 * @see set_ui_color(), tint_color(), keyboard_navigation_color()
 */
rgb_color
ui_color(color_which which)
{
	int32 index = color_which_to_index(which);
	if (index < 0 || index >= kColorWhichCount) {
		fprintf(stderr, "ui_color(): unknown color_which %d\n", which);
		return make_color(0, 0, 0);
	}

	if (be_app != NULL) {
		server_read_only_memory* shared
			= BApplication::Private::ServerReadOnlyMemory();
		if (shared != NULL) {
			// check for unset colors
			if (shared->colors[index] == B_TRANSPARENT_COLOR)
				shared->colors[index] = _kDefaultColors[index];

			return shared->colors[index];
		}
	}

	return _kDefaultColors[index];
}


/**
 * @brief Returns the built-in system color for a given theme variant.
 *
 * Bypasses the live app_server shared memory and directly indexes the
 * appropriate compile-time color table, making this safe to call before
 * the server connection is established.
 *
 * @param colorConstant A color_which constant identifying the desired color.
 * @param darkVariant   @c true to return the dark-theme default;
 *                      @c false for the light-theme default.
 *
 * @return The static default rgb_color for @a colorConstant in the chosen theme.
 *
 * @see ui_color(), _kDefaultColors, _kDefaultColorsDark
 */
rgb_color
BPrivate::GetSystemColor(color_which colorConstant, bool darkVariant) {
	if (darkVariant) {
		return _kDefaultColorsDark[color_which_to_index(colorConstant)];
	} else {
		return _kDefaultColors[color_which_to_index(colorConstant)];
	}
}


/**
 * @brief Returns the canonical string name for a color_which constant.
 *
 * Maps a color_which value to its human-readable identifier string (e.g.
 * @c B_PANEL_BACKGROUND_COLOR maps to the string @c "B_PANEL_BACKGROUND_COLOR").
 *
 * @param which A color_which constant to look up; @c B_NO_COLOR returns NULL silently.
 *
 * @return A pointer to the static constant name string, or NULL if @a which is
 *         @c B_NO_COLOR or an unrecognized value.
 *
 * @see which_ui_color(), ui_color()
 */
const char*
ui_color_name(color_which which)
{
	// Suppress warnings for B_NO_COLOR.
	if (which == B_NO_COLOR)
		return NULL;

	int32 index = color_which_to_index(which);
	if (index < 0 || index >= kColorWhichCount) {
		fprintf(stderr, "ui_color_name(): unknown color_which %d\n", which);
		return NULL;
	}

	return kColorNames[index];
}


/**
 * @brief Resolves a color name string to its color_which constant.
 *
 * Performs a linear search through the kColorNames table for an exact
 * case-sensitive match of @a name.
 *
 * @param name The canonical color name string (e.g. @c "B_PANEL_BACKGROUND_COLOR").
 *             NULL returns @c B_NO_COLOR immediately.
 *
 * @return The matching color_which constant, or @c B_NO_COLOR if not found.
 *
 * @see ui_color_name(), ui_color()
 */
color_which
which_ui_color(const char* name)
{
	if (name == NULL)
		return B_NO_COLOR;

	for (int32 index = 0; index < kColorWhichCount; ++index) {
		if (!strcmp(kColorNames[index], name))
			return index_to_color_which(index);
	}

	return B_NO_COLOR;
}


/**
 * @brief Applies a new value for a named system UI color.
 *
 * Sends the updated color to the app_server via a DesktopLink. The call is
 * a no-op if @a color is identical to the current value or if @a which is
 * an unrecognized constant.
 *
 * @param which A color_which constant identifying the color slot to update.
 * @param color The new rgb_color to assign to @a which.
 *
 * @see ui_color(), set_ui_colors()
 */
void
set_ui_color(const color_which &which, const rgb_color &color)
{
	int32 index = color_which_to_index(which);
	if (index < 0 || index >= kColorWhichCount) {
		fprintf(stderr, "set_ui_color(): unknown color_which %d\n", which);
		return;
	}

	if (ui_color(which) == color)
		return;

	BPrivate::DesktopLink link;
	link.StartMessage(AS_SET_UI_COLOR);
	link.Attach<color_which>(which);
	link.Attach<rgb_color>(color);
	link.Flush();
}


/**
 * @brief Applies a batch of system UI color updates from a BMessage.
 *
 * Iterates over all @c B_RGB_32_BIT_TYPE fields in @a colors. Each field whose
 * name maps to a known system color constant via which_ui_color() is sent to
 * the app_server in a single @c AS_SET_UI_COLORS message, minimizing round-trips.
 * Unrecognized field names are silently skipped.
 *
 * @param colors A BMessage whose named @c rgb_color fields describe the desired
 *               color changes; NULL is silently ignored.
 *
 * @see set_ui_color(), which_ui_color()
 */
void
set_ui_colors(const BMessage* colors)
{
	if (colors == NULL)
		return;

	int32 count = 0;
	int32 index = 0;
	char* name = NULL;
	type_code type;
	rgb_color color;
	color_which which = B_NO_COLOR;

	BPrivate::DesktopLink desktop;
	if (desktop.InitCheck() != B_OK)
		return;

	desktop.StartMessage(AS_SET_UI_COLORS);
	desktop.Attach<bool>(false);

	// Only colors with names that map to system colors will get through.
	while (colors->GetInfo(B_RGB_32_BIT_TYPE, index, &name, &type) == B_OK) {

		which = which_ui_color(name);
		++index;

		if (which == B_NO_COLOR || colors->FindColor(name, &color) != B_OK)
			continue;

		desktop.Attach<color_which>(which);
		desktop.Attach<rgb_color>(color);
		++count;
	}

	if (count == 0)
		return;

	desktop.Attach<color_which>(B_NO_COLOR);
	desktop.Flush();
}


/**
 * @brief Lightens or darkens a color by a tint factor.
 *
 * A @a tint of exactly @c 1.0 returns @a color unchanged. Values below @c 1.0
 * lighten the color toward white (each component is scaled toward 255), while
 * values above @c 1.0 darken it toward black (each component is scaled toward 0).
 * The alpha channel is always preserved.
 *
 * @param color The source rgb_color to tint.
 * @param tint  Tint factor; @c B_LIGHTEN_* and @c B_DARKEN_* constants from
 *              InterfaceDefs.h provide convenient standard values.
 *
 * @return A new rgb_color with the tint applied.
 *
 * @see ui_color(), shift_color()
 */
rgb_color
tint_color(rgb_color color, float tint)
{
	rgb_color result;

	#define LIGHTEN(x) ((uint8)(255.0f - (255.0f - x) * tint))
	#define DARKEN(x)  ((uint8)(x * (2 - tint)))

	if (tint < 1.0f) {
		result.red   = LIGHTEN(color.red);
		result.green = LIGHTEN(color.green);
		result.blue  = LIGHTEN(color.blue);
		result.alpha = color.alpha;
	} else {
		result.red   = DARKEN(color.red);
		result.green = DARKEN(color.green);
		result.blue  = DARKEN(color.blue);
		result.alpha = color.alpha;
	}

	#undef LIGHTEN
	#undef DARKEN

	return result;
}


/** @brief Forward declaration for shift_color(); delegates to tint_color(). */
rgb_color shift_color(rgb_color color, float shift);

/**
 * @brief Alias for tint_color() provided for BeOS source compatibility.
 *
 * @param color  The source rgb_color.
 * @param shift  Tint/shift factor (same semantics as tint_color()).
 *
 * @return Result of tint_color(@a color, @a shift).
 *
 * @see tint_color()
 */
rgb_color
shift_color(rgb_color color, float shift)
{
	return tint_color(color, shift);
}


/**
 * @brief Initializes the Interface Kit at application startup.
 *
 * Called by the shared library init routines before the application's main()
 * runs. Performs the following setup steps in order:
 * - Initializes the palette color converter.
 * - Creates the global BClipboard instance (@c be_clipboard).
 * - Loads and instantiates the ControlLook add-on specified by the server;
 *   falls back to HaikuControlLook if none is configured.
 * - Initializes the global font subsystem via _init_global_fonts_().
 * - Creates the global gWidthBuffer.
 * - Creates the menu bitmaps (BPrivate::MenuPrivate::CreateBitmaps()).
 * - Populates BMenu::sMenuInfo via get_menu_info().
 * - Fills in the legacy @c general_info struct.
 *
 * @return @c B_OK on success, or a negative error code if any step fails.
 *
 * @see _fini_interface_kit_()
 */
extern "C" status_t
_init_interface_kit_()
{
	status_t status = BPrivate::PaletteConverter::InitializeDefault(true);
	if (status < B_OK)
		return status;

	// init global clipboard
	if (be_clipboard == NULL)
		be_clipboard = new BClipboard(NULL);

	BString path;
	if (get_control_look(path) && path.Length() > 0) {
		BControlLook* (*instantiate)(image_id);

		sControlLookAddon = load_add_on(path.String());
		if (sControlLookAddon >= 0
			&& get_image_symbol(sControlLookAddon,
				"instantiate_control_look",
				B_SYMBOL_TYPE_TEXT, (void **)&instantiate) == B_OK) {
			be_control_look = instantiate(sControlLookAddon);
			if (be_control_look == NULL) {
				unload_add_on(sControlLookAddon);
				sControlLookAddon = -1;
			}
		}
	}
	if (be_control_look == NULL)
		be_control_look = new HaikuControlLook();

	_init_global_fonts_();

	BPrivate::gWidthBuffer = new BPrivate::WidthBuffer;
	status = BPrivate::MenuPrivate::CreateBitmaps();
	if (status != B_OK)
		return status;

	_menu_info_ptr_ = &BMenu::sMenuInfo;

	status = get_menu_info(&BMenu::sMenuInfo);
	if (status != B_OK)
		return status;

	general_info.background_color = ui_color(B_PANEL_BACKGROUND_COLOR);
	general_info.mark_color = ui_color(B_CONTROL_MARK_COLOR);
	general_info.highlight_color = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	general_info.window_frame_color = ui_color(B_WINDOW_TAB_COLOR);
	general_info.color_frame = true;

	// TODO: fill the other static members

	return status;
}


/**
 * @brief Tears down the Interface Kit at application exit.
 *
 * Releases resources that were allocated by _init_interface_kit_(), including
 * the menu bitmaps, gWidthBuffer, be_control_look instance, and (if loaded)
 * the ControlLook add-on image.
 *
 * @return Always returns @c B_OK.
 *
 * @see _init_interface_kit_()
 */
extern "C" status_t
_fini_interface_kit_()
{
	BPrivate::MenuPrivate::DeleteBitmaps();

	delete BPrivate::gWidthBuffer;
	BPrivate::gWidthBuffer = NULL;

	delete be_control_look;
	be_control_look = NULL;

	// Note: if we ever want to support live switching, we cannot just unload
	// the old one since some thread might still be in a method of the object.
	// maybe locking/unlocking all loopers around would ensure proper exit.
	if (sControlLookAddon >= 0)
		unload_add_on(sControlLookAddon);
	sControlLookAddon = -1;

	// TODO: Anything else?

	return B_OK;
}



namespace BPrivate {


/**
 * @brief Queries the app_server for the path of the active window decorator add-on.
 *
 * @param path Receives the file-system path of the current decorator add-on.
 *
 * @return @c true if the server returned a valid path; @c false on failure.
 *
 * @see set_decorator(), preview_decorator()
 */
bool
get_decorator(BString& path)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_DECORATOR);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return false;

	return link.ReadString(path) == B_OK;
}


/**
 * @brief Sets the active window decorator for the system.
 *
 * Instructs the app_server to load the decorator add-on located at @a path.
 * The change takes effect immediately for new windows.
 *
 * @param path File-system path to the decorator add-on to activate.
 *
 * @return @c B_OK on success, @c B_ERROR if the server link fails, or
 *         another error code returned by the server.
 *
 * @see get_decorator(), preview_decorator()
 */
status_t
set_decorator(const BString& path)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_DECORATOR);

	link.AttachString(path.String());

	status_t error = B_OK;
	if (link.FlushWithReply(error) != B_OK)
		return B_ERROR;

	return error;
}


/**
 * @brief Applies a decorator add-on preview to a single window without changing
 *        the global system decorator.
 *
 * Sends a private 'prVu' message to @a window via BWindow::SetDecoratorSettings(),
 * causing it to temporarily render with the decorator at @a path.
 *
 * @param path   File-system path to the decorator add-on to preview.
 * @param window The BWindow on which to render the preview; must not be NULL.
 *
 * @return @c B_OK on success, @c B_ERROR if @a window is NULL, or an error
 *         propagated from BWindow::SetDecoratorSettings().
 *
 * @see set_decorator(), get_decorator(), BWindow::SetDecoratorSettings()
 */
status_t
preview_decorator(const BString& path, BWindow* window)
{
	if (window == NULL)
		return B_ERROR;

	BMessage msg('prVu');
	msg.AddString("preview", path.String());

	return window->SetDecoratorSettings(msg);
}


/**
 * @brief Queries the app_server for the path of the active ControlLook add-on.
 *
 * @param path Receives the file-system path of the current ControlLook add-on.
 *
 * @return @c true if the server returned a valid path; @c false on failure.
 *
 * @see set_control_look(), _init_interface_kit_()
 */
bool
get_control_look(BString& path)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_CONTROL_LOOK);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return false;

	return link.ReadString(path) == B_OK;
}


/**
 * @brief Sets the active ControlLook add-on for the system.
 *
 * Instructs the app_server to use the ControlLook add-on located at @a path
 * for drawing all subsequent control widgets.
 *
 * @param path File-system path to the ControlLook add-on to activate.
 *
 * @return @c B_OK on success, @c B_ERROR if the server link fails, or
 *         another error code returned by the server.
 *
 * @see get_control_look(), _init_interface_kit_()
 */
status_t
set_control_look(const BString& path)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_CONTROL_LOOK);

	link.AttachString(path.String());

	status_t error = B_OK;
	if (link.FlushWithReply(error) != B_OK)
		return B_ERROR;

	return error;
}


/**
 * @brief Retrieves the stacking order of applications in a given workspace.
 *
 * Allocates and returns a heap array of team_id values ordered from frontmost
 * to backmost application. The caller is responsible for freeing the array
 * with free().
 *
 * @param workspace      Zero-based workspace index to query.
 * @param _applications  Receives a newly allocated array of team_id values.
 * @param _count         Receives the number of elements in @a _applications.
 *
 * @return @c B_OK on success, @c B_NO_MEMORY on allocation failure, or
 *         another error code if the server request fails.
 *
 * @see get_window_order()
 */
status_t
get_application_order(int32 workspace, team_id** _applications,
	int32* _count)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_APPLICATION_ORDER);
	link.Attach<int32>(workspace);

	int32 code;
	status_t status = link.FlushWithReply(code);
	if (status != B_OK)
		return status;
	if (code != B_OK)
		return code;

	int32 count;
	link.Read<int32>(&count);

	*_applications = (team_id*)malloc(count * sizeof(team_id));
	if (*_applications == NULL)
		return B_NO_MEMORY;

	link.Read(*_applications, count * sizeof(team_id));
	*_count = count;
	return B_OK;
}


/**
 * @brief Retrieves the stacking order of windows in a given workspace.
 *
 * Allocates and returns a heap array of server-side window tokens ordered
 * from frontmost to backmost. The caller is responsible for freeing the
 * array with free().
 *
 * @param workspace Zero-based workspace index to query.
 * @param _tokens   Receives a newly allocated array of window token values.
 * @param _count    Receives the number of elements in @a _tokens.
 *
 * @return @c B_OK on success, @c B_NO_MEMORY on allocation failure, or
 *         another error code if the server request fails.
 *
 * @see get_application_order(), get_window_info()
 */
status_t
get_window_order(int32 workspace, int32** _tokens, int32* _count)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_WINDOW_ORDER);
	link.Attach<int32>(workspace);

	int32 code;
	status_t status = link.FlushWithReply(code);
	if (status != B_OK)
		return status;
	if (code != B_OK)
		return code;

	int32 count;
	link.Read<int32>(&count);

	*_tokens = (int32*)malloc(count * sizeof(int32));
	if (*_tokens == NULL)
		return B_NO_MEMORY;

	link.Read(*_tokens, count * sizeof(int32));
	*_count = count;
	return B_OK;
}


}	// namespace BPrivate

// These methods were marked with "Danger, will Robinson!" in
// the OpenTracker source, so we might not want to be compatible
// here.
// In any way, we would need to update Deskbar to use our
// replacements, so we could as well just implement them...

/**
 * @brief Sends a window management action to the app_server.
 *
 * Requests that the server perform @a action (e.g. minimize, restore) on the
 * window identified by @a windowToken. The @a zoomRect and @a zoom parameters
 * are accepted for API compatibility but the zoom animation is not implemented.
 *
 * @param windowToken Server-side token identifying the target window.
 * @param action      Action constant to perform on the window.
 * @param zoomRect    Zoom origin rectangle (unused; accepted for compatibility).
 * @param zoom        Whether a zoom animation was requested (unused).
 *
 * @see get_window_info(), do_bring_to_front_team(), do_minimize_team()
 */
void
do_window_action(int32 windowToken, int32 action, BRect zoomRect, bool zoom)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_WINDOW_ACTION);
	link.Attach<int32>(windowToken);
	link.Attach<int32>(action);
		// we don't have any zooming effect

	link.Flush();
}


/**
 * @brief Returns a heap-allocated snapshot of a window's current state.
 *
 * Queries the app_server for the client_window_info structure of the window
 * identified by @a serverToken. The caller is responsible for freeing the
 * returned pointer with free().
 *
 * @param serverToken Server-side token of the window to query.
 *
 * @return A newly allocated client_window_info on success, or NULL if the
 *         server query fails or memory allocation fails.
 *
 * @see get_token_list(), do_window_action(), get_window_order()
 */
client_window_info*
get_window_info(int32 serverToken)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_WINDOW_INFO);
	link.Attach<int32>(serverToken);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return NULL;

	int32 size;
	link.Read<int32>(&size);

	client_window_info* info = (client_window_info*)malloc(size);
	if (info == NULL)
		return NULL;

	link.Read(info, size);
	return info;
}


/**
 * @brief Returns a heap-allocated list of server window tokens for a given team.
 *
 * Queries the app_server for the list of all window tokens belonging to
 * @a team. The caller is responsible for freeing the returned array with free().
 *
 * @param team   Team ID whose windows to enumerate.
 * @param _count Receives the number of tokens in the returned array.
 *
 * @return A newly allocated int32 array of window tokens, or NULL on failure.
 *
 * @see get_window_info(), get_window_order()
 */
int32*
get_token_list(team_id team, int32* _count)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_WINDOW_LIST);
	link.Attach<team_id>(team);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return NULL;

	int32 count;
	link.Read<int32>(&count);

	int32* tokens = (int32*)malloc(count * sizeof(int32));
	if (tokens == NULL)
		return NULL;

	link.Read(tokens, count * sizeof(int32));
	*_count = count;
	return tokens;
}


/**
 * @brief Brings all windows of a team to the front of the window stack.
 *
 * Sends an @c AS_BRING_TEAM_TO_FRONT message to the app_server for @a team.
 * The @a zoomRect and @a zoom parameters are accepted for API compatibility
 * but the zoom animation is not implemented.
 *
 * @param zoomRect Zoom origin rectangle (unused; accepted for compatibility).
 * @param team     Team ID whose windows to raise.
 * @param zoom     Whether a zoom animation was requested (unused).
 *
 * @see do_minimize_team(), do_window_action()
 */
void
do_bring_to_front_team(BRect zoomRect, team_id team, bool zoom)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_BRING_TEAM_TO_FRONT);
	link.Attach<team_id>(team);
		// we don't have any zooming effect

	link.Flush();
}


/**
 * @brief Minimizes all windows belonging to a team.
 *
 * Sends an @c AS_MINIMIZE_TEAM message to the app_server for @a team.
 * The @a zoomRect and @a zoom parameters are accepted for API compatibility
 * but the zoom animation is not implemented.
 *
 * @param zoomRect Zoom origin rectangle (unused; accepted for compatibility).
 * @param team     Team ID whose windows to minimize.
 * @param zoom     Whether a zoom animation was requested (unused).
 *
 * @see do_bring_to_front_team(), do_window_action()
 */
void
do_minimize_team(BRect zoomRect, team_id team, bool zoom)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_MINIMIZE_TEAM);
	link.Attach<team_id>(team);
		// we don't have any zooming effect

	link.Flush();
}


//	#pragma mark - truncate string


/**
 * @brief Truncates a BString to fit within a given pixel width, inserting an ellipsis.
 *
 * Uses the pre-computed @a escapementArray (one entry per logical character in
 * @a string) to determine where to cut the string so that it — plus the UTF-8
 * ellipsis character — fits within @a width scaled by @a fontSize. Supports
 * beginning, end, middle, and "smart" truncation via @a mode.
 *
 * A small tolerance (+1/128 pixel) is added to @a width to avoid floating-point
 * rounding artefacts that could drop a character that would otherwise fit exactly.
 *
 * @param string           The BString to truncate in-place.
 * @param mode             One of @c B_TRUNCATE_BEGINNING, @c B_TRUNCATE_END,
 *                         @c B_TRUNCATE_MIDDLE, or @c B_TRUNCATE_SMART.
 * @param width            Maximum allowed rendered width in pixels.
 * @param escapementArray  Array of per-character width fractions (width / fontSize),
 *                         one entry for each logical character in @a string.
 * @param fontSize         Font size in points used to scale escapement values.
 * @param ellipsisWidth    Rendered pixel width of the UTF-8 ellipsis character at
 *                         @a fontSize; used to reserve space for the ellipsis.
 * @param charCount        Number of logical characters in @a string (must match
 *                         the length of @a escapementArray).
 *
 * @see BFont::GetEscapements(), BFont::StringWidth()
 */
void
truncate_string(BString& string, uint32 mode, float width,
	const float* escapementArray, float fontSize, float ellipsisWidth,
	int32 charCount)
{
	// add a tiny amount to the width to make floating point inaccuracy
	// not drop chars that would actually fit exactly
	width += 1.f / 128;

	switch (mode) {
		case B_TRUNCATE_BEGINNING:
		{
			float totalWidth = 0;
			for (int32 i = charCount - 1; i >= 0; i--) {
				float charWidth = escapementArray[i] * fontSize;
				if (totalWidth + charWidth > width) {
					// we need to truncate
					while (totalWidth + ellipsisWidth > width) {
						// remove chars until there's enough space for the
						// ellipsis
						if (++i == charCount) {
							// we've reached the end of the string and still
							// no space, so return an empty string
							string.Truncate(0);
							return;
						}

						totalWidth -= escapementArray[i] * fontSize;
					}

					string.RemoveChars(0, i + 1);
					string.PrependChars(B_UTF8_ELLIPSIS, 1);
					return;
				}

				totalWidth += charWidth;
			}

			break;
		}

		case B_TRUNCATE_END:
		{
			float totalWidth = 0;
			for (int32 i = 0; i < charCount; i++) {
				float charWidth = escapementArray[i] * fontSize;
				if (totalWidth + charWidth > width) {
					// we need to truncate
					while (totalWidth + ellipsisWidth > width) {
						// remove chars until there's enough space for the
						// ellipsis
						if (i-- == 0) {
							// we've reached the start of the string and still
							// no space, so return an empty string
							string.Truncate(0);
							return;
						}

						totalWidth -= escapementArray[i] * fontSize;
					}

					string.RemoveChars(i, charCount - i);
					string.AppendChars(B_UTF8_ELLIPSIS, 1);
					return;
				}

				totalWidth += charWidth;
			}

			break;
		}

		case B_TRUNCATE_MIDDLE:
		case B_TRUNCATE_SMART:
		{
			float leftWidth = 0;
			float rightWidth = 0;
			int32 leftIndex = 0;
			int32 rightIndex = charCount - 1;
			bool left = true;

			for (int32 i = 0; i < charCount; i++) {
				float charWidth
					= escapementArray[left ? leftIndex : rightIndex] * fontSize;

				if (leftWidth + rightWidth + charWidth > width) {
					// we need to truncate
					while (leftWidth + rightWidth + ellipsisWidth > width) {
						// remove chars until there's enough space for the
						// ellipsis
						if (leftIndex == 0 && rightIndex == charCount - 1) {
							// we've reached both ends of the string and still
							// no space, so return an empty string
							string.Truncate(0);
							return;
						}

						if (leftIndex > 0 && (rightIndex == charCount - 1
								|| leftWidth > rightWidth)) {
							// remove char on the left
							leftWidth -= escapementArray[--leftIndex]
								* fontSize;
						} else {
							// remove char on the right
							rightWidth -= escapementArray[++rightIndex]
								* fontSize;
						}
					}

					string.RemoveChars(leftIndex, rightIndex + 1 - leftIndex);
					string.InsertChars(B_UTF8_ELLIPSIS, 1, leftIndex);
					return;
				}

				if (left) {
					leftIndex++;
					leftWidth += charWidth;
				} else {
					rightIndex--;
					rightWidth += charWidth;
				}

				left = rightWidth > leftWidth;
			}

			break;
		}
	}

	// we've run through without the need to truncate, leave the string as it is
}
