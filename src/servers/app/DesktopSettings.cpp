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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005-2015, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 *       Joseph Groover <looncraz@looncraz.net>
 */


/** @file DesktopSettings.cpp
    @brief Persistence and accessor layer for desktop-wide user preferences (fonts, mouse, colours, workspaces). */

#include "DesktopSettings.h"
#include "DesktopSettingsPrivate.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <DefaultColors.h>
#include <InterfaceDefs.h>
#include <ServerReadOnlyMemory.h>

#include "Desktop.h"
#include "FontCache.h"
#include "FontCacheEntry.h"
#include "GlobalFontManager.h"
#include "GlobalSubpixelSettings.h"
#include "ServerConfig.h"
#include "SystemPalette.h"


/** @brief Constructs the settings object, applying defaults and then loading saved state.
 *
 * @param shared Pointer to the server read-only shared memory area that stores
 *               the colour map and UI colours visible to client applications.
 */
DesktopSettingsPrivate::DesktopSettingsPrivate(server_read_only_memory* shared)
	:
	fShared(*shared)
{
	// if the on-disk settings are not complete, the defaults will be kept
	_SetDefaults();
	_Load();
}


/** @brief Destructor. */
DesktopSettingsPrivate::~DesktopSettingsPrivate()
{
}


/** @brief Resets all settings to built-in default values.
 *
 * Sets fonts, mouse mode, scroll bar info, menu info, workspace layout,
 * colour map, UI colours, and subpixel rendering globals to defaults.
 */
void
DesktopSettingsPrivate::_SetDefaults()
{
	fPlainFont = *gFontManager->DefaultPlainFont();
	fBoldFont = *gFontManager->DefaultBoldFont();
	fFixedFont = *gFontManager->DefaultFixedFont();

	fMouseMode = B_NORMAL_MOUSE;
	fFocusFollowsMouseMode = B_NORMAL_FOCUS_FOLLOWS_MOUSE;
	fAcceptFirstClick = true;
	fShowAllDraggers = true;

	// init scrollbar info
	fScrollBarInfo.proportional = true;
	fScrollBarInfo.double_arrows = false;
	fScrollBarInfo.knob = 0;
		// look of the knob (R5: (0, 1, 2), 1 = default)
		// change default = 0 (no knob) in Haiku
	fScrollBarInfo.min_knob_size = 15;

	// init menu info
	strlcpy(fMenuInfo.f_family, fPlainFont.Family(), B_FONT_FAMILY_LENGTH);
	strlcpy(fMenuInfo.f_style, fPlainFont.Style(), B_FONT_STYLE_LENGTH);
	fMenuInfo.font_size = fPlainFont.Size();
	fMenuInfo.background_color.set_to(216, 216, 216);

	fMenuInfo.separator = 0;
		// look of the separator (R5: (0, 1, 2), default 0)
	fMenuInfo.click_to_open = true; // always true
	fMenuInfo.triggers_always_shown = false;

	fWorkspacesColumns = 2;
	fWorkspacesRows = 2;

	memcpy((void*)&fShared.colormap, SystemColorMap(),
		sizeof(color_map));
	memcpy((void*)fShared.colors, BPrivate::kDefaultColors,
		sizeof(rgb_color) * kColorWhichCount);

	gSubpixelAntialiasing = true;
	gDefaultHintingMode = HINTING_MODE_ON;
	gSubpixelAverageWeight = 120;
	gSubpixelOrderingRGB = true;
}


/** @brief Resolves and returns the base directory used for settings files.
 *
 * Combines B_USER_SETTINGS_DIRECTORY with "system/app_server", creating the
 * directory if it does not exist.
 *
 * @param path Output BPath set to the settings directory.
 * @return B_OK on success, or an error code if the directory cannot be located or created.
 */
status_t
DesktopSettingsPrivate::_GetPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;

	status = path.Append("system/app_server");
	if (status < B_OK)
		return status;

	return create_directory(path.Path(), 0755);
}


/** @brief Loads all settings from disk, overriding defaults where saved data exists.
 *
 * Reads workspace layout, font, mouse, appearance, and dragger settings from
 * their respective files in the app_server settings directory.
 *
 * @return B_OK if the load completed without fatal errors; individual settings
 *         may still have been left at defaults if their files were missing.
 */
status_t
DesktopSettingsPrivate::_Load()
{
	// TODO: add support for old app_server_settings file as well

	BPath basePath;
	status_t status = _GetPath(basePath);
	if (status < B_OK)
		return status;

	// read workspaces settings

	BPath path(basePath);
	path.Append("workspaces");

	BFile file;
	status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status == B_OK) {
		BMessage settings;
		status = settings.Unflatten(&file);
		if (status == B_OK) {
			int32 columns;
			int32 rows;
			if (settings.FindInt32("columns", &columns) == B_OK
				&& settings.FindInt32("rows", &rows) == B_OK) {
				_ValidateWorkspacesLayout(columns, rows);
				fWorkspacesColumns = columns;
				fWorkspacesRows = rows;
			}

			int32 i = 0;
			while (i < kMaxWorkspaces && settings.FindMessage("workspace",
					i, &fWorkspaceMessages[i]) == B_OK) {
				i++;
			}
		}
	}

	// read font settings

	path = basePath;
	path.Append("fonts");

	status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status == B_OK) {
		BMessage settings;
		status = settings.Unflatten(&file);
		if (status != B_OK) {
			fFontSettingsLoadStatus = status;
		} else if (gFontManager->Lock()) {
			const char* family;
			const char* style;
			float size;

			if (settings.FindString("plain family", &family) == B_OK
				&& settings.FindString("plain style", &style) == B_OK
				&& settings.FindFloat("plain size", &size) == B_OK) {
				FontStyle* fontStyle = gFontManager->GetStyle(family, style);
				fPlainFont.SetStyle(fontStyle);
				fPlainFont.SetSize(size);
			}

			if (settings.FindString("bold family", &family) == B_OK
				&& settings.FindString("bold style", &style) == B_OK
				&& settings.FindFloat("bold size", &size) == B_OK) {
				FontStyle* fontStyle = gFontManager->GetStyle(family, style);
				fBoldFont.SetStyle(fontStyle);
				fBoldFont.SetSize(size);
			}

			if (settings.FindString("fixed family", &family) == B_OK
				&& settings.FindString("fixed style", &style) == B_OK
				&& settings.FindFloat("fixed size", &size) == B_OK) {
				FontStyle* fontStyle = gFontManager->GetStyle(family, style);
				if (fontStyle != NULL && (fontStyle->IsFixedWidth()
						|| fontStyle->IsFullAndHalfFixed()))
					fFixedFont.SetStyle(fontStyle);
				fFixedFont.SetSize(size);
			}

			int32 hinting;
			if (settings.FindInt32("hinting", &hinting) == B_OK)
				gDefaultHintingMode = hinting;

			gFontManager->Unlock();
		} else
			fFontSettingsLoadStatus = EWOULDBLOCK;
	} else
		fFontSettingsLoadStatus = status;

	// read mouse settings

	path = basePath;
	path.Append("mouse");

	status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status == B_OK) {
		BMessage settings;
		status = settings.Unflatten(&file);
		if (status == B_OK) {
			int32 mode;
			if (settings.FindInt32("mode", &mode) == B_OK)
				fMouseMode = (mode_mouse)mode;

			int32 focusFollowsMouseMode;
			if (settings.FindInt32("focus follows mouse mode",
					&focusFollowsMouseMode) == B_OK) {
				fFocusFollowsMouseMode
					= (mode_focus_follows_mouse)focusFollowsMouseMode;
			}

			bool acceptFirstClick;
			if (settings.FindBool("accept first click", &acceptFirstClick)
					== B_OK) {
				fAcceptFirstClick = acceptFirstClick;
			}
		}
	}

	// read appearance settings

	path = basePath;
	path.Append("appearance");

	status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status == B_OK) {
		BMessage settings;
		status = settings.Unflatten(&file);
		if (status == B_OK) {
			// menus
			float fontSize;
			if (settings.FindFloat("font size", &fontSize) == B_OK)
				fMenuInfo.font_size = fontSize;

			const char* fontFamily;
			if (settings.FindString("font family", &fontFamily) == B_OK)
				strlcpy(fMenuInfo.f_family, fontFamily, B_FONT_FAMILY_LENGTH);

			const char* fontStyle;
			if (settings.FindString("font style", &fontStyle) == B_OK)
				strlcpy(fMenuInfo.f_style, fontStyle, B_FONT_STYLE_LENGTH);

			rgb_color bgColor;
			if (settings.FindInt32("bg color", (int32*)&bgColor) == B_OK)
				fMenuInfo.background_color = bgColor;

			int32 separator;
			if (settings.FindInt32("separator", &separator) == B_OK)
				fMenuInfo.separator = separator;

			bool clickToOpen;
			if (settings.FindBool("click to open", &clickToOpen) == B_OK)
				fMenuInfo.click_to_open = clickToOpen;

			bool triggersAlwaysShown;
			if (settings.FindBool("triggers always shown", &triggersAlwaysShown)
					 == B_OK) {
				fMenuInfo.triggers_always_shown = triggersAlwaysShown;
			}

			// scrollbars
			bool proportional;
			if (settings.FindBool("proportional", &proportional) == B_OK)
				fScrollBarInfo.proportional = proportional;

			bool doubleArrows;
			if (settings.FindBool("double arrows", &doubleArrows) == B_OK)
				fScrollBarInfo.double_arrows = doubleArrows;

			int32 knob;
			if (settings.FindInt32("knob", &knob) == B_OK)
				fScrollBarInfo.knob = knob;

			int32 minKnobSize;
			if (settings.FindInt32("min knob size", &minKnobSize) == B_OK)
				fScrollBarInfo.min_knob_size = minKnobSize;

			// subpixel font rendering
			bool subpix;
			if (settings.FindBool("subpixel antialiasing", &subpix) == B_OK)
				gSubpixelAntialiasing = subpix;

			int8 averageWeight;
			if (settings.FindInt8("subpixel average weight", &averageWeight)
					== B_OK) {
				gSubpixelAverageWeight = averageWeight;
			}

			bool subpixelOrdering;
			if (settings.FindBool("subpixel ordering", &subpixelOrdering)
					== B_OK) {
				gSubpixelOrderingRGB = subpixelOrdering;
			}

			const char* controlLook;
			if (settings.FindString("control look", &controlLook) == B_OK) {
				fControlLook = controlLook;
			}

			// colors
			for (int32 i = 0; i < kColorWhichCount; i++) {
				char colorName[12];
				snprintf(colorName, sizeof(colorName), "color%" B_PRId32,
					(int32)index_to_color_which(i));

				if (settings.FindInt32(colorName, (int32*)&fShared.colors[i]) != B_OK) {
					// Set obviously bad value so the Appearance app can detect it
					fShared.colors[i] = B_TRANSPARENT_COLOR;
				}
			}
		}
	}

	// read dragger settings

	path = basePath;
	path.Append("dragger");

	status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status == B_OK) {
		BMessage settings;
		status = settings.Unflatten(&file);
		if (status == B_OK) {
			if (settings.FindBool("show", &fShowAllDraggers) != B_OK)
				fShowAllDraggers = true;
		}
	}

	return B_OK;
}


/** @brief Persists the specified subset of settings to disk.
 *
 * Uses a bitmask to select which setting groups to save; the available flags
 * are kWorkspacesSettings, kFontSettings, kMouseSettings, kDraggerSettings,
 * and kAppearanceSettings.
 *
 * @param mask Bitmask of setting groups to write.
 * @return B_OK on success, or an error code if any file operation failed.
 */
status_t
DesktopSettingsPrivate::Save(uint32 mask)
{
#if TEST_MODE
	return B_OK;
#endif

	BPath basePath;
	status_t status = _GetPath(basePath);
	if (status != B_OK)
		return status;

	if (mask & kWorkspacesSettings) {
		BPath path(basePath);
		if (path.Append("workspaces") == B_OK) {
			BMessage settings('asws');
			settings.AddInt32("columns", fWorkspacesColumns);
			settings.AddInt32("rows", fWorkspacesRows);

			for (int32 i = 0; i < kMaxWorkspaces; i++) {
				settings.AddMessage("workspace", &fWorkspaceMessages[i]);
			}

			BFile file;
			status = file.SetTo(path.Path(), B_CREATE_FILE | B_ERASE_FILE
				| B_READ_WRITE);
			if (status == B_OK) {
				status = settings.Flatten(&file, NULL);
			}
		}
	}

	if (mask & kFontSettings) {
		BPath path(basePath);
		if (path.Append("fonts") == B_OK) {
			BMessage settings('asfn');

			settings.AddString("plain family", fPlainFont.Family());
			settings.AddString("plain style", fPlainFont.Style());
			settings.AddFloat("plain size", fPlainFont.Size());

			settings.AddString("bold family", fBoldFont.Family());
			settings.AddString("bold style", fBoldFont.Style());
			settings.AddFloat("bold size", fBoldFont.Size());

			settings.AddString("fixed family", fFixedFont.Family());
			settings.AddString("fixed style", fFixedFont.Style());
			settings.AddFloat("fixed size", fFixedFont.Size());

			settings.AddInt32("hinting", gDefaultHintingMode);

			BFile file;
			status = file.SetTo(path.Path(), B_CREATE_FILE | B_ERASE_FILE
				| B_READ_WRITE);
			if (status == B_OK) {
				status = settings.Flatten(&file, NULL);
			}
		}
	}

	if (mask & kMouseSettings) {
		BPath path(basePath);
		if (path.Append("mouse") == B_OK) {
			BMessage settings('asms');
			settings.AddInt32("mode", (int32)fMouseMode);
			settings.AddInt32("focus follows mouse mode",
				(int32)fFocusFollowsMouseMode);
			settings.AddBool("accept first click", fAcceptFirstClick);

			BFile file;
			status = file.SetTo(path.Path(), B_CREATE_FILE | B_ERASE_FILE
				| B_READ_WRITE);
			if (status == B_OK) {
				status = settings.Flatten(&file, NULL);
			}
		}
	}

	if (mask & kDraggerSettings) {
		BPath path(basePath);
		if (path.Append("dragger") == B_OK) {
			BMessage settings('asdg');
			settings.AddBool("show", fShowAllDraggers);

			BFile file;
			status = file.SetTo(path.Path(), B_CREATE_FILE | B_ERASE_FILE
				| B_READ_WRITE);
			if (status == B_OK) {
				status = settings.Flatten(&file, NULL);
			}
		}
	}

	if (mask & kAppearanceSettings) {
		BPath path(basePath);
		if (path.Append("appearance") == B_OK) {
			BMessage settings('aslk');
			settings.AddFloat("font size", fMenuInfo.font_size);
			settings.AddString("font family", fMenuInfo.f_family);
			settings.AddString("font style", fMenuInfo.f_style);
			settings.AddInt32("bg color",
				(const int32&)fMenuInfo.background_color);
			settings.AddInt32("separator", fMenuInfo.separator);
			settings.AddBool("click to open", fMenuInfo.click_to_open);
			settings.AddBool("triggers always shown",
				fMenuInfo.triggers_always_shown);

			settings.AddBool("proportional", fScrollBarInfo.proportional);
			settings.AddBool("double arrows", fScrollBarInfo.double_arrows);
			settings.AddInt32("knob", fScrollBarInfo.knob);
			settings.AddInt32("min knob size", fScrollBarInfo.min_knob_size);

			settings.AddBool("subpixel antialiasing", gSubpixelAntialiasing);
			settings.AddInt8("subpixel average weight", gSubpixelAverageWeight);
			settings.AddBool("subpixel ordering", gSubpixelOrderingRGB);

			settings.AddString("control look", fControlLook);

			for (int32 i = 0; i < kColorWhichCount; i++) {
				char colorName[12];
				snprintf(colorName, sizeof(colorName), "color%" B_PRId32,
					(int32)index_to_color_which(i));
				settings.AddInt32(colorName, (const int32&)fShared.colors[i]);
			}

			BFile file;
			status = file.SetTo(path.Path(), B_CREATE_FILE | B_ERASE_FILE
				| B_READ_WRITE);
			if (status == B_OK) {
				status = settings.Flatten(&file, NULL);
			}
		}
	}

	return status;
}


/** @brief Sets the default plain (body text) font and persists it.
 *
 * @param font The new default plain ServerFont.
 */
void
DesktopSettingsPrivate::SetDefaultPlainFont(const ServerFont &font)
{
	fPlainFont = font;
	Save(kFontSettings);
}


/** @brief Returns the current default plain font.
 *
 * @return Const reference to the plain ServerFont.
 */
const ServerFont &
DesktopSettingsPrivate::DefaultPlainFont() const
{
	return fPlainFont;
}


/** @brief Sets the default bold font and persists it.
 *
 * @param font The new default bold ServerFont.
 */
void
DesktopSettingsPrivate::SetDefaultBoldFont(const ServerFont &font)
{
	fBoldFont = font;
	Save(kFontSettings);
}


/** @brief Returns the current default bold font.
 *
 * @return Const reference to the bold ServerFont.
 */
const ServerFont &
DesktopSettingsPrivate::DefaultBoldFont() const
{
	return fBoldFont;
}


/** @brief Sets the default fixed-width font and persists it.
 *
 * @param font The new default fixed-width ServerFont.
 */
void
DesktopSettingsPrivate::SetDefaultFixedFont(const ServerFont &font)
{
	fFixedFont = font;
	Save(kFontSettings);
}


/** @brief Returns the current default fixed-width font.
 *
 * @return Const reference to the fixed ServerFont.
 */
const ServerFont &
DesktopSettingsPrivate::DefaultFixedFont() const
{
	return fFixedFont;
}


/** @brief Sets scroll bar appearance parameters and persists them.
 *
 * @param info New scroll_bar_info to apply.
 */
void
DesktopSettingsPrivate::SetScrollBarInfo(const scroll_bar_info& info)
{
	fScrollBarInfo = info;
	Save(kAppearanceSettings);
}


/** @brief Returns the current scroll bar appearance parameters.
 *
 * @return Const reference to the current scroll_bar_info.
 */
const scroll_bar_info&
DesktopSettingsPrivate::ScrollBarInfo() const
{
	return fScrollBarInfo;
}


/** @brief Sets menu appearance parameters and updates the menu background UI colour.
 *
 * @param info New menu_info to apply; the background colour is also propagated
 *             to B_MENU_BACKGROUND_COLOR via SetUIColor().
 */
void
DesktopSettingsPrivate::SetMenuInfo(const menu_info& info)
{
	fMenuInfo = info;
	// Also update the ui_color
	SetUIColor(B_MENU_BACKGROUND_COLOR, info.background_color);
		// SetUIColor already saves the settings
}


/** @brief Returns the current menu appearance parameters.
 *
 * @return Const reference to the current menu_info.
 */
const menu_info&
DesktopSettingsPrivate::MenuInfo() const
{
	return fMenuInfo;
}


/** @brief Sets the mouse focus mode and persists it.
 *
 * @param mode The new mode_mouse value.
 */
void
DesktopSettingsPrivate::SetMouseMode(const mode_mouse mode)
{
	fMouseMode = mode;
	Save(kMouseSettings);
}


/** @brief Sets the focus-follows-mouse mode and persists it.
 *
 * @param mode The new mode_focus_follows_mouse value.
 */
void
DesktopSettingsPrivate::SetFocusFollowsMouseMode(mode_focus_follows_mouse mode)
{
	fFocusFollowsMouseMode = mode;
	Save(kMouseSettings);
}


/** @brief Returns the current mouse focus mode.
 *
 * @return The active mode_mouse value.
 */
mode_mouse
DesktopSettingsPrivate::MouseMode() const
{
	return fMouseMode;
}


/** @brief Returns the current focus-follows-mouse mode.
 *
 * @return The active mode_focus_follows_mouse value.
 */
mode_focus_follows_mouse
DesktopSettingsPrivate::FocusFollowsMouseMode() const
{
	return fFocusFollowsMouseMode;
}


/** @brief Sets whether the first click activates and is delivered to a window.
 *
 * @param acceptFirstClick true to accept the first click, false to discard it.
 */
void
DesktopSettingsPrivate::SetAcceptFirstClick(const bool acceptFirstClick)
{
	fAcceptFirstClick = acceptFirstClick;
	Save(kMouseSettings);
}


/** @brief Returns whether the first click on an inactive window is delivered to it.
 *
 * @return true if the first click is accepted, false otherwise.
 */
bool
DesktopSettingsPrivate::AcceptFirstClick() const
{
	return fAcceptFirstClick;
}


/** @brief Sets whether dragger handles are shown globally.
 *
 * @param show true to show all draggers, false to hide them.
 */
void
DesktopSettingsPrivate::SetShowAllDraggers(bool show)
{
	fShowAllDraggers = show;
	Save(kDraggerSettings);
}


/** @brief Returns whether dragger handles are shown globally.
 *
 * @return true if all draggers are visible.
 */
bool
DesktopSettingsPrivate::ShowAllDraggers() const
{
	return fShowAllDraggers;
}


/** @brief Sets the workspace grid layout and persists it.
 *
 * Validates and clamps the supplied dimensions before storing them.
 *
 * @param columns Number of workspace columns.
 * @param rows    Number of workspace rows.
 */
void
DesktopSettingsPrivate::SetWorkspacesLayout(int32 columns, int32 rows)
{
	_ValidateWorkspacesLayout(columns, rows);
	fWorkspacesColumns = columns;
	fWorkspacesRows = rows;

	Save(kWorkspacesSettings);
}


/** @brief Returns the total number of workspaces (columns * rows).
 *
 * @return Total workspace count.
 */
int32
DesktopSettingsPrivate::WorkspacesCount() const
{
	return fWorkspacesColumns * fWorkspacesRows;
}


/** @brief Returns the number of workspace columns.
 *
 * @return Column count.
 */
int32
DesktopSettingsPrivate::WorkspacesColumns() const
{
	return fWorkspacesColumns;
}


/** @brief Returns the number of workspace rows.
 *
 * @return Row count.
 */
int32
DesktopSettingsPrivate::WorkspacesRows() const
{
	return fWorkspacesRows;
}


/** @brief Stores a workspace configuration message for a specific workspace index.
 *
 * @param index   Zero-based workspace index; ignored if out of range.
 * @param message The configuration BMessage to store.
 */
void
DesktopSettingsPrivate::SetWorkspacesMessage(int32 index, BMessage& message)
{
	if (index < 0 || index >= kMaxWorkspaces)
		return;

	fWorkspaceMessages[index] = message;
}


/** @brief Returns the stored configuration message for a specific workspace.
 *
 * @param index Zero-based workspace index.
 * @return Pointer to the BMessage, or NULL if the index is out of range.
 */
const BMessage*
DesktopSettingsPrivate::WorkspacesMessage(int32 index) const
{
	if (index < 0 || index >= kMaxWorkspaces)
		return NULL;

	return &fWorkspaceMessages[index];
}


/** @brief Sets a single UI colour and persists the appearance settings.
 *
 * Also updates the menu_info background colour if \a which is
 * B_MENU_BACKGROUND_COLOR.
 *
 * @param which   The colour role to update.
 * @param color   The new colour value.
 * @param changed If non-NULL, set to true when the colour actually changed.
 */
void
DesktopSettingsPrivate::SetUIColor(color_which which, const rgb_color color,
									bool* changed)
{
	int32 index = color_which_to_index(which);
	if (index < 0 || index >= kColorWhichCount)
		return;

	if (changed != NULL)
		*changed = fShared.colors[index] != color;

	fShared.colors[index] = color;
	// TODO: deprecate the background_color member of the menu_info struct,
	// otherwise we have to keep this duplication...
	if (which == B_MENU_BACKGROUND_COLOR)
		fMenuInfo.background_color = color;

	Save(kAppearanceSettings);
}


/** @brief Batch-updates multiple UI colours from a BMessage and persists them.
 *
 * Iterates all B_RGB_32_BIT_TYPE fields in \a colors, maps each field name to
 * a color_which index, and updates the shared colour array. Entries that cannot
 * be mapped are skipped with changed[i] set to false.
 *
 * @param colors  BMessage whose fields are named UI colour constants.
 * @param changed Optional array (one entry per color in \a colors) indicating
 *                which colours actually changed; may be NULL.
 */
void
DesktopSettingsPrivate::SetUIColors(const BMessage& colors, bool* changed)
{
	int32 count = colors.CountNames(B_RGB_32_BIT_TYPE);
	if (count <= 0)
		return;

	int32 index = 0;
	int32 colorIndex = 0;
	char* name = NULL;
	type_code type;
	rgb_color color;
	color_which which = B_NO_COLOR;

	while (colors.GetInfo(B_RGB_32_BIT_TYPE, index, &name, &type) == B_OK) {
		which = which_ui_color(name);
		colorIndex = color_which_to_index(which);
		if (colorIndex < 0 || colorIndex >= kColorWhichCount
			|| colors.FindColor(name, &color) != B_OK) {
			if (changed != NULL)
				changed[index] = false;

			++index;
			continue;
		}

		if (changed != NULL)
			changed[index] = fShared.colors[colorIndex] != color;

		fShared.colors[colorIndex] = color;

		if (which == (int32)B_MENU_BACKGROUND_COLOR)
			fMenuInfo.background_color = color;

		++index;
	}

	Save(kAppearanceSettings);
}


/** @brief Returns the current value of a UI colour.
 *
 * @param which The colour role to query.
 * @return The colour, or {0,0,0,0} if the role is invalid.
 */
rgb_color
DesktopSettingsPrivate::UIColor(color_which which) const
{
	static const rgb_color invalidColor = {0, 0, 0, 0};
	int32 index = color_which_to_index(which);
	if (index < 0 || index >= kColorWhichCount)
		return invalidColor;

	return fShared.colors[index];
}


/** @brief Enables or disables subpixel antialiasing and persists the setting.
 *
 * @param subpix true to enable subpixel antialiasing, false to disable it.
 */
void
DesktopSettingsPrivate::SetSubpixelAntialiasing(bool subpix)
{
	gSubpixelAntialiasing = subpix;
	Save(kAppearanceSettings);
}


/** @brief Returns whether subpixel antialiasing is enabled.
 *
 * @return true if subpixel antialiasing is active.
 */
bool
DesktopSettingsPrivate::SubpixelAntialiasing() const
{
	return gSubpixelAntialiasing;
}


/** @brief Sets the global font hinting mode and persists it.
 *
 * @param hinting The new HINTING_MODE_* value.
 */
void
DesktopSettingsPrivate::SetHinting(uint8 hinting)
{
	gDefaultHintingMode = hinting;
	Save(kFontSettings);
}


/** @brief Returns the current global font hinting mode.
 *
 * @return The active hinting mode constant.
 */
uint8
DesktopSettingsPrivate::Hinting() const
{
	return gDefaultHintingMode;
}


/** @brief Sets the subpixel average weight for LCD font rendering and persists it.
 *
 * @param averageWeight The new weight value (0–255).
 */
void
DesktopSettingsPrivate::SetSubpixelAverageWeight(uint8 averageWeight)
{
	gSubpixelAverageWeight = averageWeight;
	Save(kAppearanceSettings);
}


/** @brief Returns the current subpixel average weight.
 *
 * @return Weight value in the range 0–255.
 */
uint8
DesktopSettingsPrivate::SubpixelAverageWeight() const
{
	return gSubpixelAverageWeight;
}


/** @brief Sets whether subpixel rendering uses RGB (true) or BGR (false) ordering.
 *
 * @param subpixelOrdering true for RGB, false for BGR.
 */
void
DesktopSettingsPrivate::SetSubpixelOrderingRegular(bool subpixelOrdering)
{
	gSubpixelOrderingRGB = subpixelOrdering;
	Save(kAppearanceSettings);
}


/** @brief Returns whether subpixel rendering is in RGB order.
 *
 * @return true for RGB ordering, false for BGR.
 */
bool
DesktopSettingsPrivate::IsSubpixelOrderingRegular() const
{
	return gSubpixelOrderingRGB;
}


/** @brief Sets the path to the ControlLook add-on and persists it.
 *
 * @param path File-system path to the ControlLook shared library; empty string
 *             resets to the system default.
 * @return B_OK on success, or an error code from Save().
 */
status_t
DesktopSettingsPrivate::SetControlLook(const char* path)
{
	fControlLook = path;
	return Save(kAppearanceSettings);
}


/** @brief Returns the path to the currently active ControlLook add-on.
 *
 * @return BString containing the path, or an empty string for the default.
 */
const BString&
DesktopSettingsPrivate::ControlLook() const
{
	return fControlLook;
}


/** @brief Validates and clamps workspace column/row values to acceptable bounds.
 *
 * Ensures both values are at least 1. If their product exceeds kMaxWorkspaces,
 * both are reset to 2 (the default 2x2 layout).
 *
 * @param columns Column count to validate and clamp (modified in place).
 * @param rows    Row count to validate and clamp (modified in place).
 */
void
DesktopSettingsPrivate::_ValidateWorkspacesLayout(int32& columns,
	int32& rows) const
{
	if (columns < 1)
		columns = 1;
	if (rows < 1)
		rows = 1;

	if (columns * rows > kMaxWorkspaces) {
		// Revert to defaults in case of invalid settings
		columns = 2;
		rows = 2;
	}
}


//	#pragma mark - read access


/** @brief Constructs a read-only view of the desktop settings.
 *
 * @param desktop The Desktop whose settings will be accessed.
 */
DesktopSettings::DesktopSettings(Desktop* desktop)
	:
	fSettings(desktop->fSettings.Get())
{

}


/** @brief Copies the default plain font into \a font.
 *
 * @param font Output parameter to receive the default plain ServerFont.
 */
void
DesktopSettings::GetDefaultPlainFont(ServerFont &font) const
{
	font = fSettings->DefaultPlainFont();
}


/** @brief Copies the default bold font into \a font.
 *
 * @param font Output parameter to receive the default bold ServerFont.
 */
void
DesktopSettings::GetDefaultBoldFont(ServerFont &font) const
{
	font = fSettings->DefaultBoldFont();
}


/** @brief Copies the default fixed-width font into \a font.
 *
 * @param font Output parameter to receive the default fixed ServerFont.
 */
void
DesktopSettings::GetDefaultFixedFont(ServerFont &font) const
{
	font = fSettings->DefaultFixedFont();
}


/** @brief Copies the scroll bar appearance parameters into \a info.
 *
 * @param info Output parameter to receive the current scroll_bar_info.
 */
void
DesktopSettings::GetScrollBarInfo(scroll_bar_info& info) const
{
	info = fSettings->ScrollBarInfo();
}


/** @brief Copies the menu appearance parameters into \a info.
 *
 * @param info Output parameter to receive the current menu_info.
 */
void
DesktopSettings::GetMenuInfo(menu_info& info) const
{
	info = fSettings->MenuInfo();
}


/** @brief Returns the current mouse focus mode.
 *
 * @return The active mode_mouse value.
 */
mode_mouse
DesktopSettings::MouseMode() const
{
	return fSettings->MouseMode();
}


/** @brief Returns the current focus-follows-mouse mode.
 *
 * @return The active mode_focus_follows_mouse value.
 */
mode_focus_follows_mouse
DesktopSettings::FocusFollowsMouseMode() const
{
	return fSettings->FocusFollowsMouseMode();
}


/** @brief Returns whether the first click on an inactive window is delivered to it.
 *
 * @return true if first-click acceptance is enabled.
 */
bool
DesktopSettings::AcceptFirstClick() const
{
	return fSettings->AcceptFirstClick();
}


/** @brief Returns whether all dragger handles are globally visible.
 *
 * @return true if all draggers are shown.
 */
bool
DesktopSettings::ShowAllDraggers() const
{
	return fSettings->ShowAllDraggers();
}


/** @brief Returns the total number of workspaces.
 *
 * @return Workspace count (columns * rows).
 */
int32
DesktopSettings::WorkspacesCount() const
{
	return fSettings->WorkspacesCount();
}


/** @brief Returns the number of workspace columns.
 *
 * @return Column count.
 */
int32
DesktopSettings::WorkspacesColumns() const
{
	return fSettings->WorkspacesColumns();
}


/** @brief Returns the number of workspace rows.
 *
 * @return Row count.
 */
int32
DesktopSettings::WorkspacesRows() const
{
	return fSettings->WorkspacesRows();
}


/** @brief Returns the stored configuration message for a workspace.
 *
 * @param index Zero-based workspace index.
 * @return Pointer to the BMessage, or NULL if out of range.
 */
const BMessage*
DesktopSettings::WorkspacesMessage(int32 index) const
{
	return fSettings->WorkspacesMessage(index);
}


/** @brief Returns the current value of a UI colour.
 *
 * @param which The colour role to query.
 * @return The colour value.
 */
rgb_color
DesktopSettings::UIColor(color_which which) const
{
	return fSettings->UIColor(which);
}


/** @brief Returns whether subpixel antialiasing is enabled.
 *
 * @return true if subpixel antialiasing is active.
 */
bool
DesktopSettings::SubpixelAntialiasing() const
{
	return fSettings->SubpixelAntialiasing();
}


/** @brief Returns the current global font hinting mode.
 *
 * @return The active hinting mode constant.
 */
uint8
DesktopSettings::Hinting() const
{
	return fSettings->Hinting();
}


/** @brief Returns the current subpixel average weight.
 *
 * @return Weight value in the range 0–255.
 */
uint8
DesktopSettings::SubpixelAverageWeight() const
{
	return fSettings->SubpixelAverageWeight();
}


/** @brief Returns whether subpixel rendering uses RGB ordering.
 *
 * @return true for RGB, false for BGR.
 */
bool
DesktopSettings::IsSubpixelOrderingRegular() const
{
	// True corresponds to RGB, false means BGR
	return fSettings->IsSubpixelOrderingRegular();
}


/** @brief Returns the path to the currently active ControlLook add-on.
 *
 * @return BString containing the path, or an empty string for the default.
 */
const BString&
DesktopSettings::ControlLook() const
{
	return fSettings->ControlLook();
}

//	#pragma mark - write access


/** @brief Constructs a write-capable settings accessor and acquires all-windows lock.
 *
 * Inherits the read-access desktop settings and additionally locks all windows
 * so that changes can be applied atomically.
 *
 * @param desktop The Desktop whose settings will be modified.
 */
LockedDesktopSettings::LockedDesktopSettings(Desktop* desktop)
	:
	DesktopSettings(desktop),
	fDesktop(desktop)
{
#if DEBUG
	if (desktop->fWindowLock.IsReadLocked())
		debugger("desktop read locked when trying to change settings");
#endif

	fDesktop->LockAllWindows();
}


/** @brief Destructor — releases the all-windows lock. */
LockedDesktopSettings::~LockedDesktopSettings()
{
	fDesktop->UnlockAllWindows();
}


/** @brief Sets the default plain font.
 *
 * @param font The new default plain ServerFont.
 */
void
LockedDesktopSettings::SetDefaultPlainFont(const ServerFont &font)
{
	fSettings->SetDefaultPlainFont(font);
}


void
/** @brief Sets the default bold font and broadcasts the change to all windows.
 *
 * @param font The new default bold ServerFont.
 */
void
LockedDesktopSettings::SetDefaultBoldFont(const ServerFont &font)
{
	fSettings->SetDefaultBoldFont(font);
	fDesktop->BroadcastToAllWindows(AS_SYSTEM_FONT_CHANGED);
}


/** @brief Sets the default fixed-width font.
 *
 * @param font The new default fixed ServerFont.
 */
void
LockedDesktopSettings::SetDefaultFixedFont(const ServerFont &font)
{
	fSettings->SetDefaultFixedFont(font);
}


/** @brief Sets scroll bar appearance parameters.
 *
 * @param info New scroll_bar_info to apply.
 */
void
LockedDesktopSettings::SetScrollBarInfo(const scroll_bar_info& info)
{
	fSettings->SetScrollBarInfo(info);
}


/** @brief Sets menu appearance parameters.
 *
 * @param info New menu_info to apply.
 */
void
LockedDesktopSettings::SetMenuInfo(const menu_info& info)
{
	fSettings->SetMenuInfo(info);
}


/** @brief Sets the mouse focus mode.
 *
 * @param mode The new mode_mouse value.
 */
void
LockedDesktopSettings::SetMouseMode(const mode_mouse mode)
{
	fSettings->SetMouseMode(mode);
}


/** @brief Sets the focus-follows-mouse mode.
 *
 * @param mode The new mode_focus_follows_mouse value.
 */
void
LockedDesktopSettings::SetFocusFollowsMouseMode(mode_focus_follows_mouse mode)
{
	fSettings->SetFocusFollowsMouseMode(mode);
}


/** @brief Sets whether the first click on an inactive window is delivered to it.
 *
 * @param acceptFirstClick true to accept, false to discard the first click.
 */
void
LockedDesktopSettings::SetAcceptFirstClick(const bool acceptFirstClick)
{
	fSettings->SetAcceptFirstClick(acceptFirstClick);
}


/** @brief Sets whether dragger handles are globally visible.
 *
 * @param show true to show all draggers, false to hide them.
 */
void
LockedDesktopSettings::SetShowAllDraggers(bool show)
{
	fSettings->SetShowAllDraggers(show);
}


/** @brief Batch-updates multiple UI colours.
 *
 * @param colors  BMessage mapping colour role names to rgb_color values.
 * @param changed Array (indexed by colour) indicating which entries changed.
 */
void
LockedDesktopSettings::SetUIColors(const BMessage& colors, bool* changed)
{
	fSettings->SetUIColors(colors, &changed[0]);
}


/** @brief Enables or disables subpixel antialiasing.
 *
 * @param subpix true to enable, false to disable.
 */
void
LockedDesktopSettings::SetSubpixelAntialiasing(bool subpix)
{
	fSettings->SetSubpixelAntialiasing(subpix);
}


/** @brief Sets the global font hinting mode.
 *
 * @param hinting The new HINTING_MODE_* value.
 */
void
LockedDesktopSettings::SetHinting(uint8 hinting)
{
	fSettings->SetHinting(hinting);
}


/** @brief Sets the subpixel average weight for LCD font rendering.
 *
 * @param averageWeight Weight value in the range 0–255.
 */
void
LockedDesktopSettings::SetSubpixelAverageWeight(uint8 averageWeight)
{
	fSettings->SetSubpixelAverageWeight(averageWeight);
}

/** @brief Sets whether subpixel rendering uses RGB (true) or BGR (false) ordering.
 *
 * @param subpixelOrdering true for RGB, false for BGR.
 */
void
LockedDesktopSettings::SetSubpixelOrderingRegular(bool subpixelOrdering)
{
	fSettings->SetSubpixelOrderingRegular(subpixelOrdering);
}


/** @brief Sets the ControlLook add-on path and persists it.
 *
 * @param path File-system path to the ControlLook shared library.
 * @return B_OK on success, or an error code from the underlying save.
 */
status_t
LockedDesktopSettings::SetControlLook(const char* path)
{
	return fSettings->SetControlLook(path);
}

