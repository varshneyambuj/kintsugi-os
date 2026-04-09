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
 * MIT License. Copyright 2001-2015, Haiku.
 * Original authors: Axel Dörfler, axeld@pinc-software.de,
 *                   Andrej Spielmann, Joseph Groover <looncraz@looncraz.net>.
 */

/** @file DesktopSettings.h
    @brief Read-only and read-write views of the persistent desktop settings. */

#ifndef DESKTOP_SETTINGS_H
#define DESKTOP_SETTINGS_H


#include <InterfaceDefs.h>
#include <Menu.h>
#include <Message.h>

#include <ServerProtocolStructs.h>


class Desktop;
class DesktopSettingsPrivate;
class ServerFont;


/** @brief Maximum number of simultaneously active workspaces. */
static const int32 kMaxWorkspaces = 32;

/** @brief Bitmask constants selecting which settings categories to save or load. */
enum {
	kAllSettings		= 0xff,
	kWorkspacesSettings	= 0x01,
	kFontSettings		= 0x02,
	kAppearanceSettings	= 0x04,
	kMouseSettings		= 0x08,
	kDraggerSettings	= 0x10,
};


/** @brief Provides read-only access to the desktop's persistent settings
           without requiring any lock beyond the desktop lock itself. */
class DesktopSettings {
public:
								DesktopSettings(Desktop* desktop);

			/** @brief Saves the specified settings categories to disk.
			    @param mask Bitmask of setting categories to save (default: all).
			    @return B_OK on success, or an error code. */
			status_t			Save(uint32 mask = kAllSettings);

			/** @brief Retrieves the default plain (body-text) font.
			    @param font Output ServerFont descriptor. */
			void				GetDefaultPlainFont(ServerFont& font) const;

			/** @brief Retrieves the default bold font.
			    @param font Output ServerFont descriptor. */
			void				GetDefaultBoldFont(ServerFont& font) const;

			/** @brief Retrieves the default fixed-width font.
			    @param font Output ServerFont descriptor. */
			void				GetDefaultFixedFont(ServerFont& font) const;

			/** @brief Retrieves scroll bar appearance settings.
			    @param info Output scroll_bar_info structure. */
			void				GetScrollBarInfo(scroll_bar_info& info) const;

			/** @brief Retrieves menu appearance settings.
			    @param info Output menu_info structure. */
			void				GetMenuInfo(menu_info& info) const;

			/** @brief Returns the current mouse focus mode.
			    @return One of the mode_mouse enum values. */
			mode_mouse			MouseMode() const;

			/** @brief Returns the focus-follows-mouse sub-mode.
			    @return One of the mode_focus_follows_mouse enum values. */
			mode_focus_follows_mouse FocusFollowsMouseMode() const;

			/** @brief Returns true if the mouse mode is B_NORMAL_MOUSE.
			    @return true for normal click-to-focus behaviour. */
			bool				NormalMouse() const
									{ return MouseMode() == B_NORMAL_MOUSE; }

			/** @brief Returns true if focus-follows-mouse is active.
			    @return true if focus follows pointer movement. */
			bool				FocusFollowsMouse() const
									{ return MouseMode()
										== B_FOCUS_FOLLOWS_MOUSE; }

			/** @brief Returns true if click-to-focus mode is active.
			    @return true if focus changes only on click. */
			bool				ClickToFocusMouse() const
									{ return MouseMode()
										== B_CLICK_TO_FOCUS_MOUSE; }

			/** @brief Returns whether the first click on a non-focused window is accepted.
			    @return true if first-click events are delivered to the application. */
			bool				AcceptFirstClick() const;

			/** @brief Returns whether all drag-and-drop handles (draggers) are visible.
			    @return true if all draggers are shown. */
			bool				ShowAllDraggers() const;

			/** @brief Returns the total number of configured workspaces.
			    @return Workspace count. */
			int32				WorkspacesCount() const;

			/** @brief Returns the number of workspace columns in the grid.
			    @return Column count. */
			int32				WorkspacesColumns() const;

			/** @brief Returns the number of workspace rows in the grid.
			    @return Row count. */
			int32				WorkspacesRows() const;

			/** @brief Returns the configuration message for a specific workspace.
			    @param index Workspace index.
			    @return Pointer to the workspace BMessage, or NULL. */
			const BMessage*		WorkspacesMessage(int32 index) const;

			/** @brief Returns the current value of a UI color.
			    @param which Which UI color to query.
			    @return The rgb_color currently assigned to that role. */
			rgb_color			UIColor(color_which which) const;

			/** @brief Returns whether sub-pixel anti-aliasing is enabled.
			    @return true if sub-pixel AA is active. */
			bool				SubpixelAntialiasing() const;

			/** @brief Returns the current font hinting level.
			    @return Hinting level byte. */
			uint8				Hinting() const;

			/** @brief Returns the sub-pixel average weight used for LCD rendering.
			    @return Average weight byte. */
			uint8				SubpixelAverageWeight() const;

			/** @brief Returns whether the sub-pixel colour ordering is RGB (regular).
			    @return true for RGB ordering, false for BGR. */
			bool				IsSubpixelOrderingRegular() const;

			/** @brief Returns the path to the current control look add-on.
			    @return Reference to the control look path string. */
			const BString&		ControlLook() const;

protected:
			DesktopSettingsPrivate*	fSettings;
};


/** @brief Extends DesktopSettings with write access; acquires the desktop settings
           lock on construction and releases it on destruction. */
class LockedDesktopSettings : public DesktopSettings {
public:
								LockedDesktopSettings(Desktop* desktop);
								~LockedDesktopSettings();

			/** @brief Sets the default plain font.
			    @param font The ServerFont to use as the plain font. */
			void				SetDefaultPlainFont(const ServerFont& font);

			/** @brief Sets the default bold font.
			    @param font The ServerFont to use as the bold font. */
			void				SetDefaultBoldFont(const ServerFont& font);

			/** @brief Sets the default fixed-width font.
			    @param font The ServerFont to use as the fixed font. */
			void				SetDefaultFixedFont(const ServerFont& font);

			/** @brief Updates scroll bar appearance settings.
			    @param info New scroll_bar_info values. */
			void				SetScrollBarInfo(const scroll_bar_info& info);

			/** @brief Updates menu appearance settings.
			    @param info New menu_info values. */
			void				SetMenuInfo(const menu_info& info);

			/** @brief Changes the mouse focus mode.
			    @param mode New mode_mouse value. */
			void				SetMouseMode(mode_mouse mode);

			/** @brief Changes the focus-follows-mouse sub-mode.
			    @param mode New mode_focus_follows_mouse value. */
			void				SetFocusFollowsMouseMode(
									mode_focus_follows_mouse mode);

			/** @brief Sets whether the first click on an unfocused window is accepted.
			    @param acceptFirstClick true to accept first-click events. */
			void				SetAcceptFirstClick(bool acceptFirstClick);

			/** @brief Sets the visibility of all drag-and-drop handles.
			    @param show true to make all draggers visible. */
			void				SetShowAllDraggers(bool show);

			/** @brief Applies a set of UI colour changes from a message.
			    @param colors BMessage mapping color_which to rgb_color.
			    @param changed Optional output array set to true for each changed colour. */
			void				SetUIColors(const BMessage& colors,
									bool* changed = NULL);

			/** @brief Enables or disables sub-pixel anti-aliasing.
			    @param subpix true to enable sub-pixel AA. */
			void				SetSubpixelAntialiasing(bool subpix);

			/** @brief Sets the font hinting level.
			    @param hinting New hinting level byte. */
			void				SetHinting(uint8 hinting);

			/** @brief Sets the sub-pixel average weight for LCD rendering.
			    @param averageWeight New average weight byte. */
			void				SetSubpixelAverageWeight(uint8 averageWeight);

			/** @brief Sets the sub-pixel colour ordering.
			    @param subpixelOrdering true for RGB (regular), false for BGR. */
			void				SetSubpixelOrderingRegular(
									bool subpixelOrdering);

			/** @brief Changes the control look add-on path.
			    @param path Filesystem path to the new control look add-on.
			    @return B_OK on success, or an error code. */
			status_t			SetControlLook(const char* path);

private:
			Desktop*			fDesktop;
};


#endif	/* DESKTOP_SETTINGS_H */
