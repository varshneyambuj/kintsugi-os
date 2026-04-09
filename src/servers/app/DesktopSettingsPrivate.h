/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2005-2015, Haiku.
 * Original authors: Axel Dörfler, axeld@pinc-software.de,
 *                   Andrej Spielmann, Joseph Groover <looncraz@looncraz.net>.
 */

/** @file DesktopSettingsPrivate.h
    @brief Internal implementation of the desktop settings storage and serialisation. */

#ifndef DESKTOP_SETTINGS_PRIVATE_H
#define DESKTOP_SETTINGS_PRIVATE_H


#include "DesktopSettings.h"

#include <Locker.h>

#include "ServerFont.h"


struct server_read_only_memory;


/** @brief Holds the authoritative, mutable copy of all desktop settings and provides
           load/save support and accessor methods used by DesktopSettings. */
class DesktopSettingsPrivate {
public:
								DesktopSettingsPrivate(
									server_read_only_memory* shared);
								~DesktopSettingsPrivate();

			/** @brief Returns true if font settings were loaded without errors.
			    @return true if font settings loaded successfully. */
			bool				DidLoadFontSettings() const
									{ return fFontSettingsLoadStatus == B_OK; }

			/** @brief Persists the selected settings categories to disk.
			    @param mask Bitmask of categories to save (default: all).
			    @return B_OK on success, or an error code. */
			status_t			Save(uint32 mask = kAllSettings);

			/** @brief Sets the default plain font.
			    @param font New ServerFont for plain text. */
			void				SetDefaultPlainFont(const ServerFont& font);

			/** @brief Returns the default plain font.
			    @return Const reference to the plain ServerFont. */
			const ServerFont&	DefaultPlainFont() const;

			/** @brief Sets the default bold font.
			    @param font New ServerFont for bold text. */
			void				SetDefaultBoldFont(const ServerFont& font);

			/** @brief Returns the default bold font.
			    @return Const reference to the bold ServerFont. */
			const ServerFont&	DefaultBoldFont() const;

			/** @brief Sets the default fixed-width font.
			    @param font New ServerFont for fixed-width text. */
			void				SetDefaultFixedFont(const ServerFont& font);

			/** @brief Returns the default fixed-width font.
			    @return Const reference to the fixed ServerFont. */
			const ServerFont&	DefaultFixedFont() const;

			/** @brief Updates scroll bar appearance settings.
			    @param info New scroll_bar_info values. */
			void				SetScrollBarInfo(const scroll_bar_info &info);

			/** @brief Returns the current scroll bar appearance settings.
			    @return Const reference to the scroll_bar_info. */
			const scroll_bar_info& ScrollBarInfo() const;

			/** @brief Updates menu appearance settings.
			    @param info New menu_info values. */
			void				SetMenuInfo(const menu_info &info);

			/** @brief Returns the current menu appearance settings.
			    @return Const reference to the menu_info. */
			const menu_info&	MenuInfo() const;

			/** @brief Changes the mouse focus mode.
			    @param mode New mode_mouse value. */
			void				SetMouseMode(mode_mouse mode);

			/** @brief Returns the current mouse focus mode.
			    @return Current mode_mouse value. */
			mode_mouse			MouseMode() const;

			/** @brief Changes the focus-follows-mouse sub-mode.
			    @param mode New mode_focus_follows_mouse value. */
			void				SetFocusFollowsMouseMode(
									mode_focus_follows_mouse mode);

			/** @brief Returns the focus-follows-mouse sub-mode.
			    @return Current mode_focus_follows_mouse value. */
			mode_focus_follows_mouse FocusFollowsMouseMode() const;

			/** @brief Returns true if the mouse is in normal (click-to-focus) mode.
			    @return true for B_NORMAL_MOUSE. */
			bool				NormalMouse() const
									{ return MouseMode() == B_NORMAL_MOUSE; }

			/** @brief Returns true if focus-follows-mouse is active.
			    @return true for B_FOCUS_FOLLOWS_MOUSE. */
			bool				FocusFollowsMouse() const
									{ return MouseMode()
										== B_FOCUS_FOLLOWS_MOUSE; }

			/** @brief Returns true if click-to-focus mode is active.
			    @return true for B_CLICK_TO_FOCUS_MOUSE. */
			bool				ClickToFocusMouse() const
									{ return MouseMode()
										== B_CLICK_TO_FOCUS_MOUSE; }

			/** @brief Sets whether first clicks on unfocused windows are accepted.
			    @param acceptFirstClick true to accept first-click events. */
			void				SetAcceptFirstClick(bool acceptFirstClick);

			/** @brief Returns whether first clicks on unfocused windows are accepted.
			    @return true if first-click events are delivered. */
			bool				AcceptFirstClick() const;

			/** @brief Sets the visibility of all drag handles.
			    @param show true to show all draggers. */
			void				SetShowAllDraggers(bool show);

			/** @brief Returns whether all drag handles are visible.
			    @return true if all draggers are shown. */
			bool				ShowAllDraggers() const;

			/** @brief Reconfigures the workspace grid.
			    @param columns Number of columns.
			    @param rows Number of rows. */
			void				SetWorkspacesLayout(int32 columns, int32 rows);

			/** @brief Returns the total number of workspaces.
			    @return Workspace count (columns * rows). */
			int32				WorkspacesCount() const;

			/** @brief Returns the number of workspace grid columns.
			    @return Column count. */
			int32				WorkspacesColumns() const;

			/** @brief Returns the number of workspace grid rows.
			    @return Row count. */
			int32				WorkspacesRows() const;

			/** @brief Stores a configuration message for a workspace.
			    @param index Workspace index.
			    @param message The configuration BMessage to store. */
			void				SetWorkspacesMessage(int32 index,
									BMessage& message);

			/** @brief Returns the configuration message for a workspace.
			    @param index Workspace index.
			    @return Pointer to the workspace BMessage, or NULL. */
			const BMessage*		WorkspacesMessage(int32 index) const;

			/** @brief Sets a single UI colour.
			    @param which Which UI colour role.
			    @param color New rgb_color value.
			    @param changed Optional output set to true if the colour changed. */
			void				SetUIColor(color_which which,
									const rgb_color color,
									bool* changed = NULL);

			/** @brief Applies multiple UI colour changes from a message.
			    @param colors BMessage mapping color_which to rgb_color.
			    @param changed Optional boolean array; set to true for each changed entry. */
			void				SetUIColors(const BMessage& colors,
									bool* changed = NULL);
									// changed must be boolean array equal in
									// size to colors' size

			/** @brief Returns the current value of a UI colour.
			    @param which Which UI colour role.
			    @return The rgb_color for that role. */
			rgb_color			UIColor(color_which which) const;

			/** @brief Enables or disables sub-pixel anti-aliasing.
			    @param subpix true to enable. */
			void				SetSubpixelAntialiasing(bool subpix);

			/** @brief Returns whether sub-pixel anti-aliasing is enabled.
			    @return true if enabled. */
			bool				SubpixelAntialiasing() const;

			/** @brief Sets the font hinting level.
			    @param hinting New hinting level. */
			void				SetHinting(uint8 hinting);

			/** @brief Returns the font hinting level.
			    @return Current hinting level. */
			uint8				Hinting() const;

			/** @brief Sets the sub-pixel average weight.
			    @param averageWeight New average weight byte. */
			void				SetSubpixelAverageWeight(uint8 averageWeight);

			/** @brief Returns the sub-pixel average weight.
			    @return Current average weight byte. */
			uint8				SubpixelAverageWeight() const;

			/** @brief Sets the sub-pixel colour ordering.
			    @param subpixelOrdering true for RGB, false for BGR. */
			void				SetSubpixelOrderingRegular(
									bool subpixelOrdering);

			/** @brief Returns whether sub-pixel ordering is RGB (regular).
			    @return true for RGB ordering. */
			bool				IsSubpixelOrderingRegular() const;

			/** @brief Changes the control look add-on path.
			    @param path Filesystem path to the new control look.
			    @return B_OK on success, or an error code. */
			status_t			SetControlLook(const char* path);

			/** @brief Returns the current control look add-on path.
			    @return Const reference to the path string. */
			const BString&		ControlLook() const;

private:
			void				_SetDefaults();
			status_t			_Load();
			status_t			_GetPath(BPath& path);
			void				_ValidateWorkspacesLayout(int32& columns,
									int32& rows) const;

			status_t			fFontSettingsLoadStatus;

			ServerFont			fPlainFont;
			ServerFont			fBoldFont;
			ServerFont			fFixedFont;

			scroll_bar_info		fScrollBarInfo;
			menu_info			fMenuInfo;
			mode_mouse			fMouseMode;
			mode_focus_follows_mouse	fFocusFollowsMouseMode;
			bool				fAcceptFirstClick;
			bool				fShowAllDraggers;
			int32				fWorkspacesColumns;
			int32				fWorkspacesRows;
			BMessage			fWorkspaceMessages[kMaxWorkspaces];
			BString				fControlLook;

			server_read_only_memory& fShared;
};

#endif	/* DESKTOP_SETTINGS_PRIVATE_H */
