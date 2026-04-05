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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2025 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini, stefano.ceccherini@gmail.com
 *       Marc Flerackers, mflerackers@androme.be
 *       Bill Hayden, haydentech@users.sourceforge.net
 *       Olivier Milla
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file MenuItem.cpp
 * @brief Implementation of BMenuItem, a single item in a BMenu
 *
 * BMenuItem draws a text label (with optional shortcut and submenu arrow),
 * tracks its marked/enabled/selected state, and invokes a BMessage when
 * selected. Subclasses can override DrawContent() for custom rendering.
 *
 * @see BMenu, BSeparatorItem
 */


#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <Bitmap.h>
#include <ControlLook.h>
#include <MenuItem.h>
#include <Shape.h>
#include <String.h>
#include <Window.h>

#include <MenuPrivate.h>

#include "utf8_functions.h"


/** @brief Tint factor applied to the high color when drawing the mark checkmark. */
static const float kMarkTint = 0.75f;

// map control key shortcuts to drawable Unicode characters
// cf. http://unicode.org/charts/PDF/U2190.pdf
/** @brief Maps ASCII control-key values to their displayable UTF-8 Unicode symbols. */
const char* kUTF8ControlMap[] = {
	NULL,
	"\xe2\x86\xb8", /* B_HOME U+21B8 */
	NULL, NULL,
	NULL, /* B_END */
	NULL, /* B_INSERT */
	NULL, NULL,
	"\xe2\x8c\xab", /* B_BACKSPACE U+232B */
	"\xe2\x86\xb9", /* B_TAB U+21B9 */
	"\xe2\x8f\x8e", /* B_ENTER, U+23CE */
	NULL, /* B_PAGE_UP */
	NULL, /* B_PAGE_DOWN */
	NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	"\xe2\x86\x90", /* B_LEFT_ARROW */
	"\xe2\x86\x92", /* B_RIGHT_ARROW */
	"\xe2\x86\x91", /* B_UP_ARROW */
	"\xe2\x86\x93", /* B_DOWN_ARROW */
	"\xe2\x90\xa3"  /* B_SPACE */
};

/** @brief UTF-8 symbol used to represent the Delete key in shortcut display (U+2326). */
static const char* kDeleteShortcutUTF8 = "\xe2\x8c\xa6"; /* B_DELETE U+2326 */


using BPrivate::MenuPrivate;


/**
 * @brief Construct a BMenuItem with a text label, message, and optional shortcut.
 *
 * @param label     Text label displayed for this item; a copy is made.
 * @param message   BMessage sent when the item is invoked; ownership is transferred.
 * @param shortcut  ASCII character used as the keyboard shortcut, or 0 for none.
 * @param modifiers Modifier key mask (e.g. B_COMMAND_KEY) qualifying the shortcut.
 */
BMenuItem::BMenuItem(const char* label, BMessage* message, char shortcut, uint32 modifiers)
{
	_InitData();
	if (label != NULL)
		fLabel = strdup(label);

	SetMessage(message);

	fShortcutChar = shortcut;
	fModifiers = (fShortcutChar != 0 ? modifiers : 0);
}


/**
 * @brief Construct a BMenuItem that opens a submenu.
 *
 * The item's label is derived from the submenu's name (or the marked item's
 * label when the menu is in radio+label-from-marked mode).
 *
 * @param menu    The submenu to attach; ownership is transferred.
 * @param message BMessage sent when the item is invoked, or NULL for none.
 */
BMenuItem::BMenuItem(BMenu* menu, BMessage* message)
{
	_InitData();
	SetMessage(message);
	_InitMenuData(menu);
}


/**
 * @brief Unarchive constructor: restore a BMenuItem from a BMessage archive.
 *
 * Reads the label, enabled state, marked state, trigger, shortcut, message,
 * and optional submenu from \a data.
 *
 * @param data The archive message produced by Archive().
 * @see Instantiate(), Archive()
 */
BMenuItem::BMenuItem(BMessage* data)
{
	_InitData();

	if (data->HasString("_label")) {
		const char* string;

		data->FindString("_label", &string);
		SetLabel(string);
	}

	bool disable;
	if (data->FindBool("_disable", &disable) == B_OK)
		SetEnabled(!disable);

	bool marked;
	if (data->FindBool("_marked", &marked) == B_OK)
		SetMarked(marked);

	int32 userTrigger;
	if (data->FindInt32("_user_trig", &userTrigger) == B_OK)
		SetTrigger(userTrigger);

	if (data->HasInt32("_shortcut")) {
		int32 shortcut, mods;

		data->FindInt32("_shortcut", &shortcut);
		data->FindInt32("_mods", &mods);

		SetShortcut(shortcut, mods);
	}

	if (data->HasMessage("_msg")) {
		BMessage* message = new BMessage;
		data->FindMessage("_msg", message);
		SetMessage(message);
	}

	BMessage subMessage;
	if (data->FindMessage("_submenu", &subMessage) == B_OK) {
		BArchivable* object = instantiate_object(&subMessage);
		if (object != NULL) {
			BMenu* menu = dynamic_cast<BMenu*>(object);
			if (menu != NULL)
				_InitMenuData(menu);
		}
	}
}


/**
 * @brief Create a new BMenuItem from an archived BMessage.
 *
 * @param data The archive message to instantiate from.
 * @return A new BMenuItem if \a data is a valid BMenuItem archive, or NULL
 *         if validation fails.
 * @see Archive()
 */
BArchivable*
BMenuItem::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BMenuItem"))
		return new BMenuItem(data);

	return NULL;
}


/**
 * @brief Archive this BMenuItem into a BMessage.
 *
 * Stores the label, enabled/marked state, trigger, shortcut, message, and
 * (when \a deep is true) the submenu.
 *
 * @param data  The message to archive into.
 * @param deep  If true, the attached submenu is recursively archived.
 * @return B_OK on success, or a negative error code on failure.
 * @see Instantiate()
 */
status_t
BMenuItem::Archive(BMessage* data, bool deep) const
{
	status_t status = BArchivable::Archive(data, deep);

	if (status == B_OK && fLabel)
		status = data->AddString("_label", Label());

	if (status == B_OK && !IsEnabled())
		status = data->AddBool("_disable", true);

	if (status == B_OK && IsMarked())
		status = data->AddBool("_marked", true);

	if (status == B_OK && fUserTrigger)
		status = data->AddInt32("_user_trig", fUserTrigger);

	if (status == B_OK && fShortcutChar != 0) {
		status = data->AddInt32("_shortcut", fShortcutChar);
		if (status == B_OK)
			status = data->AddInt32("_mods", fModifiers);
	}

	if (status == B_OK && Message() != NULL)
		status = data->AddMessage("_msg", Message());

	if (status == B_OK && deep && fSubmenu) {
		BMessage submenu;
		if (fSubmenu->Archive(&submenu, true) == B_OK)
			status = data->AddMessage("_submenu", &submenu);
	}

	return status;
}


/**
 * @brief Destroy the BMenuItem, removing it from its parent menu.
 *
 * Frees the label string, deletes the attached submenu, and removes the
 * item from its parent BMenu if one exists.
 */
BMenuItem::~BMenuItem()
{
	if (fSuper != NULL)
		fSuper->RemoveItem(this);

	free(fLabel);
	delete fSubmenu;
}


/**
 * @brief Set the item's text label.
 *
 * Replaces the current label with a copy of \a string and triggers a layout
 * invalidation and visual refresh of the parent menu.
 *
 * @param string The new label text, or NULL to clear the label.
 */
void
BMenuItem::SetLabel(const char* string)
{
	if (fLabel != NULL) {
		free(fLabel);
		fLabel = NULL;
	}

	if (string != NULL)
		fLabel = strdup(string);

	if (fSuper != NULL) {
		fSuper->InvalidateLayout();

		if (fSuper->LockLooper()) {
			fSuper->Invalidate();
			fSuper->UnlockLooper();
		}
	}
}


/**
 * @brief Enable or disable the item.
 *
 * When \a enable is false the item cannot be invoked and is rendered in a
 * dimmed style. If the item owns a submenu, the submenu's enabled state is
 * updated to match.
 *
 * @param enable Pass true to enable the item, false to disable it.
 */
void
BMenuItem::SetEnabled(bool enable)
{
	if (fEnabled == enable)
		return;

	fEnabled = enable;

	if (fSubmenu != NULL)
		fSubmenu->SetEnabled(enable);

	BMenu* menu = fSuper;
	if (menu != NULL && menu->LockLooper()) {
		menu->Invalidate(fBounds);
		menu->UnlockLooper();
	}
}


/**
 * @brief Mark or unmark the item.
 *
 * When \a mark is true and the parent menu is in radio mode, all other items
 * in the menu are automatically unmarked via MenuPrivate::ItemMarked().
 *
 * @param mark Pass true to mark the item, false to unmark it.
 */
void
BMenuItem::SetMarked(bool mark)
{
	fMark = mark;

	if (mark && fSuper != NULL) {
		MenuPrivate priv(fSuper);
		priv.ItemMarked(this);
	}
}


/**
 * @brief Set the keyboard trigger character for this item.
 *
 * The trigger is the underlined letter shown in the item's label that, when
 * pressed while the menu is open, selects the item. The search is
 * case-insensitive and prefers an uppercase match.
 *
 * @param trigger The ASCII character to use as the trigger.
 */
void
BMenuItem::SetTrigger(char trigger)
{
	fUserTrigger = trigger;

	// try uppercase letters first

	const char* pos = strchr(Label(), toupper(trigger));
	trigger = tolower(trigger);

	if (pos == NULL) {
		// take lowercase, too
		pos = strchr(Label(), trigger);
	}

	if (pos != NULL) {
		fTriggerIndex = UTF8CountChars(Label(), pos - Label());
		fTrigger = trigger;
	} else {
		fTrigger = 0;
		fTriggerIndex = -1;
	}

	if (fSuper != NULL)
		fSuper->InvalidateLayout();
}


/**
 * @brief Set the global keyboard shortcut for this item.
 *
 * Registers the shortcut with the item's window so it can be triggered even
 * when the menu is closed. The old shortcut (if any) is removed from the
 * window first.
 *
 * @param shortcut   ASCII character for the shortcut key, or 0 to remove.
 * @param modifiers  Modifier key mask (e.g. B_COMMAND_KEY) for the shortcut.
 */
void
BMenuItem::SetShortcut(char shortcut, uint32 modifiers)
{
	if (fShortcutChar != 0 && fWindow != NULL)
		fWindow->RemoveShortcut(fShortcutChar, fModifiers);

	uint32 key = (uint32)shortcut;

	if (key != 0 && fWindow != NULL)
		fWindow->_AddShortcut(&key, &modifiers, this);

	fShortcutChar = (char)key;
	fModifiers = (fShortcutChar != 0 ? modifiers : 0);

	if (fSuper != NULL) {
		fSuper->InvalidateLayout();

		if (fSuper->LockLooper()) {
			fSuper->Invalidate();
			fSuper->UnlockLooper();
		}
	}
}


/**
 * @brief Return the item's text label.
 *
 * @return A pointer to the internal label string, or NULL if none is set.
 */
const char*
BMenuItem::Label() const
{
	return fLabel;
}


/**
 * @brief Return whether the item can currently be invoked.
 *
 * An item is enabled only when it is individually enabled, its parent menu
 * chain is enabled, and (if it has a submenu) the submenu is enabled.
 *
 * @return true if the item is enabled, false otherwise.
 */
bool
BMenuItem::IsEnabled() const
{
	if (fSubmenu)
		return fSubmenu->IsEnabled();

	if (!fEnabled)
		return false;

	return fSuper != NULL ? fSuper->IsEnabled() : true;
}


/**
 * @brief Return whether the item is currently marked.
 *
 * @return true if the item has a checkmark, false otherwise.
 */
bool
BMenuItem::IsMarked() const
{
	return fMark;
}


/**
 * @brief Return the user-specified trigger character.
 *
 * @return The trigger character set by SetTrigger(), or 0 if none is set.
 */
char
BMenuItem::Trigger() const
{
	return fUserTrigger;
}


/**
 * @brief Return the keyboard shortcut character and its modifiers.
 *
 * @param modifiers If non-NULL, the modifier mask is written here.
 * @return The shortcut character, or 0 if no shortcut is set.
 */
char
BMenuItem::Shortcut(uint32* modifiers) const
{
	if (modifiers)
		*modifiers = fModifiers;

	return fShortcutChar;
}


/**
 * @brief Return the submenu attached to this item.
 *
 * @return A pointer to the BMenu submenu, or NULL if this is a leaf item.
 */
BMenu*
BMenuItem::Submenu() const
{
	return fSubmenu;
}


/**
 * @brief Return the parent BMenu that contains this item.
 *
 * @return A pointer to the parent BMenu, or NULL if the item has no parent.
 */
BMenu*
BMenuItem::Menu() const
{
	return fSuper;
}


/**
 * @brief Return the bounding rectangle of this item in the parent menu's coordinates.
 *
 * @return The item's frame rect as set by the parent menu during layout.
 */
BRect
BMenuItem::Frame() const
{
	return fBounds;
}


/**
 * @brief Calculate the natural content width and height of this item.
 *
 * Caches the font metrics and string width so the parent menu can size its
 * items during layout. Results are written to \a _width and \a _height.
 *
 * @param _width   If non-NULL, receives the content width in pixels.
 * @param _height  If non-NULL, receives the content height (font height) in pixels.
 */
void
BMenuItem::GetContentSize(float* _width, float* _height)
{
	// TODO: Get rid of this. BMenu should handle this
	// automatically. Maybe it's not even needed, since our
	// BFont::Height() caches the value locally
	MenuPrivate(fSuper).CacheFontInfo();

	fCachedWidth = fSuper->StringWidth(fLabel);

	if (_width)
		*_width = (float)ceil(fCachedWidth);
	if (_height)
		*_height = MenuPrivate(fSuper).FontHeight();
}


/**
 * @brief Truncate the label to fit within \a maxWidth pixels using B_TRUNCATE_MIDDLE.
 *
 * The truncated string is written into the caller-supplied buffer \a newLabel,
 * which must be large enough to hold the result (at most strlen(Label()) + 4).
 *
 * @param maxWidth  Maximum pixel width available for the label.
 * @param newLabel  Output buffer that receives the null-terminated truncated string.
 */
void
BMenuItem::TruncateLabel(float maxWidth, char* newLabel)
{
	BFont font;
	fSuper->GetFont(&font);

	BString string(fLabel);

	font.TruncateString(&string, B_TRUNCATE_MIDDLE, maxWidth);

	string.CopyInto(newLabel, 0, string.Length());
	newLabel[string.Length()] = '\0';
}


/**
 * @brief Draw the label text (and trigger underline) at the current pen position.
 *
 * Called by Draw() after the pen has been moved to ContentLocation(). If the
 * label is too wide for the available frame width it is truncated first.
 * Triggers are rendered with an underline using StrokeLine().
 *
 * @note Subclasses may override this method to provide custom content drawing.
 * @see Draw(), TruncateLabel()
 */
void
BMenuItem::DrawContent()
{
	MenuPrivate menuPrivate(fSuper);
	menuPrivate.CacheFontInfo();

	fSuper->MovePenBy(0, menuPrivate.Ascent());
	BPoint lineStart = fSuper->PenLocation();

	fSuper->SetDrawingMode(B_OP_OVER);

	float labelWidth;
	float labelHeight;
	GetContentSize(&labelWidth, &labelHeight);

	const BRect& padding = menuPrivate.Padding();
	float maxContentWidth = fSuper->MaxContentWidth();
	float frameWidth = maxContentWidth > 0 ? maxContentWidth
		: fSuper->Frame().Width() - padding.left - padding.right;

	if (roundf(frameWidth) >= roundf(labelWidth))
		fSuper->DrawString(fLabel);
	else {
		// truncate label to fit
		char* truncatedLabel = new char[strlen(fLabel) + 4];
		TruncateLabel(frameWidth, truncatedLabel);
		fSuper->DrawString(truncatedLabel);
		delete[] truncatedLabel;
	}

	if (fSuper->AreTriggersEnabled() && fTriggerIndex != -1) {
		float escapements[fTriggerIndex + 1];
		BFont font;
		fSuper->GetFont(&font);

		font.GetEscapements(fLabel, fTriggerIndex + 1, escapements);

		for (int32 i = 0; i < fTriggerIndex; i++)
			lineStart.x += escapements[i] * font.Size();

		lineStart.x--;
		lineStart.y++;

		BPoint lineEnd(lineStart);
		lineEnd.x += escapements[fTriggerIndex] * font.Size();

		fSuper->StrokeLine(lineStart, lineEnd);
	}
}


/**
 * @brief Draw the complete menu item including background, content, and decorations.
 *
 * Fills the selection background when active, calls DrawContent() for the
 * label, and then renders the mark symbol, shortcut, and submenu arrow as
 * appropriate for the parent menu's layout mode.
 *
 * @see DrawContent(), Highlight()
 */
void
BMenuItem::Draw()
{
	const color_which lowColor = fSuper->LowUIColor();
	const color_which highColor = fSuper->HighUIColor();

	fSuper->SetLowColor(_LowColor());
	fSuper->SetHighColor(_HighColor());

	if (_IsActivated()) {
		// fill in the background
		BRect frame(Frame());
		be_control_look->DrawMenuItemBackground(fSuper, frame, frame,
			fSuper->LowColor(), BControlLook::B_ACTIVATED);
	}

	// draw content
	fSuper->MovePenTo(ContentLocation());
	DrawContent();

	// draw extra symbols
	MenuPrivate privateAccessor(fSuper);
	const menu_layout layout = privateAccessor.Layout();
	if (layout != B_ITEMS_IN_ROW) {
		if (IsMarked())
			_DrawMarkSymbol();
	}

	if (layout == B_ITEMS_IN_COLUMN) {
		if (fShortcutChar != 0)
			_DrawShortcutSymbol(privateAccessor.HasSubmenus());

		if (Submenu() != NULL)
			_DrawSubmenuSymbol();
	}

	// restore the parent menu's low color and high color
	fSuper->SetLowUIColor(lowColor);
	fSuper->SetHighUIColor(highColor);
}


/**
 * @brief Called when the item's selection state changes.
 *
 * Invalidates the item's frame in the parent menu so the highlighted
 * (or de-highlighted) appearance is repainted.
 *
 * @param highlight true if the item is now selected, false if deselected.
 */
void
BMenuItem::Highlight(bool highlight)
{
	fSuper->Invalidate(Frame());
}


/**
 * @brief Return whether the item is currently selected (highlighted).
 *
 * @return true if this item is the currently highlighted menu item.
 */
bool
BMenuItem::IsSelected() const
{
	return fSelected;
}


/**
 * @brief Return the drawing origin for the item's content.
 *
 * Adds the parent menu's left and top padding to the item's top-left corner.
 *
 * @return The point at which DrawContent() should begin drawing.
 */
BPoint
BMenuItem::ContentLocation() const
{
	const BRect& padding = MenuPrivate(fSuper).Padding();

	return BPoint(fBounds.left + padding.left, fBounds.top + padding.top);
}


void BMenuItem::_ReservedMenuItem1() {}
void BMenuItem::_ReservedMenuItem2() {}
void BMenuItem::_ReservedMenuItem3() {}
void BMenuItem::_ReservedMenuItem4() {}


BMenuItem::BMenuItem(const BMenuItem &)
{
}


BMenuItem&
BMenuItem::operator=(const BMenuItem &)
{
	return *this;
}


/**
 * @brief Initialize all member variables to their default values.
 *
 * Called from every constructor before any other initialization.
 */
void
BMenuItem::_InitData()
{
	fLabel = NULL;
	fSubmenu = NULL;
	fWindow = NULL;
	fSuper = NULL;
	fModifiers = 0;
	fCachedWidth = 0;
	fTriggerIndex = -1;
	fUserTrigger = 0;
	fTrigger = 0;
	fShortcutChar = 0;
	fMark = false;
	fEnabled = true;
	fSelected = false;
}


/**
 * @brief Attach a submenu to this item and derive the item's label from it.
 *
 * Sets up the bidirectional link between the item and its submenu by calling
 * MenuPrivate::SetSuperItem(), then chooses the label from the submenu's
 * marked item (in radio+label-from-marked mode) or its name.
 *
 * @param menu The BMenu to attach; ownership is transferred to this item.
 */
void
BMenuItem::_InitMenuData(BMenu* menu)
{
	fSubmenu = menu;

	MenuPrivate(fSubmenu).SetSuperItem(this);

	BMenuItem* item = menu->FindMarked();

	if (menu->IsRadioMode() && menu->IsLabelFromMarked() && item != NULL)
		SetLabel(item->Label());
	else
		SetLabel(menu->Name());
}


/**
 * @brief Register this item with a BWindow so shortcuts can be dispatched.
 *
 * Recursively installs the submenu (if any), stores the window reference,
 * registers the shortcut key with the window, and sets the invoke target to
 * the window if no explicit target has been assigned.
 *
 * @param window The window to install into.
 */
void
BMenuItem::Install(BWindow* window)
{
	if (fSubmenu != NULL)
		MenuPrivate(fSubmenu).Install(window);

	fWindow = window;

	uint32 key = (uint32)fShortcutChar;
	uint32 modifiers = fModifiers;

	if (fShortcutChar != 0 && fWindow != NULL)
		fWindow->_AddShortcut(&key, &modifiers, this);

	fShortcutChar = (char)key;
	fModifiers = (fShortcutChar != 0 ? modifiers : 0);

	if (!Messenger().IsValid())
		SetTarget(fWindow);
}


/**
 * @brief Invoke the item's message, marking it if the parent is in radio mode.
 *
 * Stamps the clone message with the item's index, timestamp, source pointer,
 * and sender messenger before dispatching via BInvoker::Invoke().
 *
 * @param message  Optional override message; if NULL the item's own message is used.
 * @return B_OK on success, B_ERROR if the item is disabled, or B_BAD_VALUE if
 *         there is no message to send and the menu is not watched.
 */
status_t
BMenuItem::Invoke(BMessage* message)
{
	if (!IsEnabled())
		return B_ERROR;

	if (fSuper->IsRadioMode())
		SetMarked(true);

	bool notify = false;
	uint32 kind = InvokeKind(&notify);

	BMessage clone(kind);
	status_t err = B_BAD_VALUE;

	if (message == NULL && !notify)
		message = Message();

	if (message == NULL) {
		if (!fSuper->IsWatched())
			return err;
	} else
		clone = *message;

	clone.AddInt32("index", fSuper->IndexOf(this));
	clone.AddInt64("when", (int64)system_time());
	clone.AddPointer("source", this);
	clone.AddMessenger("be:sender", BMessenger(fSuper));

	if (message != NULL)
		err = BInvoker::Invoke(&clone);

//	TODO: assynchronous messaging
//	SendNotices(kind, &clone);

	return err;
}


/**
 * @brief Detach this item from its BWindow, removing the registered shortcut.
 *
 * Recursively uninstalls the submenu, clears the window reference, and
 * removes the keyboard shortcut from the window.
 */
void
BMenuItem::Uninstall()
{
	if (fSubmenu != NULL)
		MenuPrivate(fSubmenu).Uninstall();

	if (Target() == fWindow)
		SetTarget(BMessenger());

	if (fShortcutChar != 0 && fWindow != NULL)
		fWindow->RemoveShortcut(fShortcutChar, fModifiers);

	fWindow = NULL;
}


/**
 * @brief Set the parent BMenu of this item.
 *
 * Calls debugger() if the item already has a different parent, enforcing the
 * rule that a menu item may only belong to one container at a time.
 *
 * @param super The new parent BMenu, or NULL to detach.
 */
void
BMenuItem::SetSuper(BMenu* super)
{
	if (fSuper != NULL && super != NULL) {
		debugger("Error - can't add menu or menu item to more than 1 container"
			" (either menu or menubar).");
	}

	if (fSubmenu != NULL)
		MenuPrivate(fSubmenu).SetSuper(super);

	fSuper = super;
}


/**
 * @brief Change the item's selected (highlighted) state.
 *
 * If the state changes, updates fSelected and calls Highlight() to trigger
 * a visual refresh. Selection is only allowed when the item is enabled or
 * has a submenu.
 *
 * @param selected true to select the item, false to deselect.
 */
void
BMenuItem::Select(bool selected)
{
	if (fSelected == selected)
		return;

	if (Submenu() != NULL || IsEnabled()) {
		fSelected = selected;
		Highlight(selected);
	}
}


/**
 * @brief Return true when the item should be drawn in its activated appearance.
 *
 * An item is activated when it is selected and either enabled or has a submenu.
 *
 * @return true if the item should use the activated visual style.
 */
bool
BMenuItem::_IsActivated()
{
	return IsSelected() && (IsEnabled() || fSubmenu != NULL);
}


/**
 * @brief Return the background fill color for this item.
 *
 * Returns B_MENU_SELECTED_BACKGROUND_COLOR when activated, otherwise
 * B_MENU_BACKGROUND_COLOR.
 *
 * @return The rgb_color to use as the low (background) color.
 */
rgb_color
BMenuItem::_LowColor()
{
	return _IsActivated() ? ui_color(B_MENU_SELECTED_BACKGROUND_COLOR)
		: ui_color(B_MENU_BACKGROUND_COLOR);
}


/**
 * @brief Return the foreground text/stroke color for this item.
 *
 * Chooses among the selected-text color, normal text color, and a disabled
 * tint based on the background luminance.
 *
 * @return The rgb_color to use as the high (foreground) color.
 */
rgb_color
BMenuItem::_HighColor()
{
	rgb_color highColor;

	bool isEnabled = IsEnabled();
	bool isSelected = IsSelected();

	if (isEnabled && isSelected)
		highColor = ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR);
	else if (isEnabled)
		highColor = ui_color(B_MENU_ITEM_TEXT_COLOR);
	else {
		rgb_color bgColor = fSuper->LowColor();
		if (bgColor.red + bgColor.green + bgColor.blue > 128 * 3)
			highColor = tint_color(bgColor, B_DISABLED_LABEL_TINT);
		else
			highColor = tint_color(bgColor, B_LIGHTEN_2_TINT);
	}

	return highColor;
}


/**
 * @brief Draw the checkmark symbol in the left margin of the item.
 *
 * Renders a small tick/checkmark glyph using StrokeShape(), positioned and
 * sized to fit within the item's left padding area.
 */
void
BMenuItem::_DrawMarkSymbol()
{
	fSuper->PushState();

	BRect r(fBounds);
	float leftMargin;
	MenuPrivate(fSuper).GetItemMargins(&leftMargin, NULL, NULL, NULL);
	float gap = leftMargin / 4;
	r.right = r.left + leftMargin - gap;
	r.left += gap / 3;

	BPoint center(floorf((r.left + r.right) / 2.0),
		floorf((r.top + r.bottom) / 2.0));

	float size = std::min(r.Height() - 2, r.Width());
	r.top = floorf(center.y - size / 2 + 0.5);
	r.bottom = floorf(center.y + size / 2 + 0.5);
	r.left = floorf(center.x - size / 2 + 0.5);
	r.right = floorf(center.x + size / 2 + 0.5);

	BShape arrowShape;
	center.x += 0.5;
	center.y += 0.5;
	size *= 0.3;
	arrowShape.MoveTo(BPoint(center.x - size, center.y - size * 0.25));
	arrowShape.LineTo(BPoint(center.x - size * 0.25, center.y + size));
	arrowShape.LineTo(BPoint(center.x + size, center.y - size));

	fSuper->SetHighColor(tint_color(_HighColor(), kMarkTint));
	fSuper->SetDrawingMode(B_OP_OVER);
	fSuper->SetPenSize(2.0);
	// NOTE: StrokeShape() offsets the shape by the current pen position,
	// it is not documented in the BeBook, but it is true!
	fSuper->MovePenTo(B_ORIGIN);
	fSuper->StrokeShape(&arrowShape);

	fSuper->PopState();
}


/**
 * @brief Draw the keyboard shortcut symbol at the right edge of the item.
 *
 * Renders the shortcut character (or its UTF-8 control-key symbol) followed
 * by modifier-key bitmaps (Command, Control, Option, Shift) reading right-to-left.
 *
 * @param submenus Pass true when at least one item in the menu has a submenu,
 *                 so that extra space is reserved for the submenu arrow.
 */
void
BMenuItem::_DrawShortcutSymbol(bool submenus)
{
	BMenu* menu = fSuper;
	BFont font;
	menu->GetFont(&font);
	BPoint where = ContentLocation();
	// Start from the right and walk our way back
	where.x = fBounds.right - font.Size();

	// Leave space for the submenu arrow if any item in the menu has a submenu
	if (submenus)
		where.x -= fBounds.Height() / 2;

	const float ascent = MenuPrivate(fSuper).Ascent();
	if ((fShortcutChar <= B_SPACE && kUTF8ControlMap[(int)fShortcutChar])
		|| fShortcutChar == B_DELETE) {
		_DrawControlChar(fShortcutChar, where + BPoint(0, ascent));
	} else
		fSuper->DrawChar(fShortcutChar, where + BPoint(0, ascent));

	where.y += (fBounds.Height() - 11) / 2 - 1;
	where.x -= 4;

	// TODO: It would be nice to draw these taking into account the text (low)
	// color.
	if ((fModifiers & B_COMMAND_KEY) != 0) {
		const BBitmap* command = MenuPrivate::MenuItemCommand();
		const BRect &rect = command->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(command, where);
	}

	if ((fModifiers & B_CONTROL_KEY) != 0) {
		const BBitmap* control = MenuPrivate::MenuItemControl();
		const BRect &rect = control->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(control, where);
	}

	if ((fModifiers & B_OPTION_KEY) != 0) {
		const BBitmap* option = MenuPrivate::MenuItemOption();
		const BRect &rect = option->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(option, where);
	}

	if ((fModifiers & B_SHIFT_KEY) != 0) {
		const BBitmap* shift = MenuPrivate::MenuItemShift();
		const BRect &rect = shift->Bounds();
		where.x -= rect.Width() + 1;
		fSuper->DrawBitmap(shift, where);
	}
}


/**
 * @brief Draw the right-pointing arrow that indicates a submenu is attached.
 *
 * Renders the arrow using BControlLook::DrawArrowShape() scaled to two-thirds
 * of the item height and positioned at the right edge of the item's frame.
 */
void
BMenuItem::_DrawSubmenuSymbol()
{
	fSuper->PushState();

	float symbolSize = roundf(Frame().Height() * 2 / 3);

	BRect rect(fBounds);
	rect.left = rect.right - symbolSize;

	// 14px by default, scaled with font size up to right margin - padding
	BRect symbolRect(0, 0, symbolSize, symbolSize);
	symbolRect.OffsetTo(BPoint(rect.left,
		fBounds.top + (fBounds.Height() - symbolSize) / 2));

	be_control_look->DrawArrowShape(Menu(), symbolRect, symbolRect,
		_HighColor(), BControlLook::B_RIGHT_ARROW, 0, kMarkTint);

	fSuper->PopState();
}


/**
 * @brief Draw the UTF-8 Unicode symbol that represents a non-printable control shortcut.
 *
 * Looks up \a shortcut in kUTF8ControlMap (or uses kDeleteShortcutUTF8 for
 * B_DELETE) and draws the resulting symbol string at \a where.
 *
 * @param shortcut The control character whose Unicode glyph should be drawn.
 * @param where    The baseline point in the parent menu's coordinate system.
 */
void
BMenuItem::_DrawControlChar(char shortcut, BPoint where)
{
	// TODO: If needed, take another font for the control characters
	//	(or have font overlays in the app_server!)
	const char* symbol = " ";
	if (shortcut == B_DELETE)
		symbol = kDeleteShortcutUTF8;
	else if (kUTF8ControlMap[(int)fShortcutChar])
		symbol = kUTF8ControlMap[(int)fShortcutChar];

	fSuper->DrawString(symbol, where);
}


/**
 * @brief Set the automatically-assigned trigger index and character.
 *
 * Called by BMenu during layout when it assigns triggers to items that do not
 * have a user-specified trigger, allowing the menu to avoid duplicate triggers.
 *
 * @param index   UTF-8 character index within the label to underline.
 * @param trigger The trigger code point.
 */
void
BMenuItem::SetAutomaticTrigger(int32 index, uint32 trigger)
{
	fTriggerIndex = index;
	fTrigger = trigger;
}
