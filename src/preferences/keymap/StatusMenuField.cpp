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
 *   Copyright 2023 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file StatusMenuField.cpp
 * @brief Menu field with status icon overlay used by ModifierKeysWindow.
 */


#include "StatusMenuField.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <LayoutUtils.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Modifier keys window"


#ifdef DEBUG_ALERT
#	define FTRACE(x) fprintf(x)
#else
#	define FTRACE(x) /* nothing */
#endif


/** @brief Status token meaning "this role is mapped to a duplicate key". */
static const char* kDuplicate = "duplicate";
/** @brief Status token meaning "left and right key roles disagree". */
static const char* kUnmatched = "unmatched";


//	#pragma mark - StatusMenuItem


/**
 * @brief Construct a StatusMenuItem with an empty icon.
 *
 * @param name    Item label.
 * @param message Optional message sent on selection.
 */
StatusMenuItem::StatusMenuItem(const char* name, BMessage* message)
	:
	BMenuItem(name, message),
	fIcon(NULL)
{
}


/**
 * @brief Reconstruct a StatusMenuItem from its archived form.
 *
 * @param archive BMessage carrying the archived state.
 */
StatusMenuItem::StatusMenuItem(BMessage* archive)
	:
	BMenuItem(archive),
	fIcon(NULL)
{
}


/**
 * @brief BArchivable factory for StatusMenuItem.
 *
 * @param data Archive containing a serialized StatusMenuItem.
 * @return     A new instance, or NULL if @a data is not a StatusMenuItem.
 */
BArchivable*
StatusMenuItem::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "StatusMenuItem"))
		return new StatusMenuItem(data);

	return NULL;
}


/**
 * @brief Forward archiving to BMenuItem.
 *
 * @param data Out: receives the archived state.
 * @param deep When true, archive nested items as well.
 * @return     Whatever BMenuItem::Archive() returns.
 */
status_t
StatusMenuItem::Archive(BMessage* data, bool deep) const
{
	status_t result = BMenuItem::Archive(data, deep);

	return result;
}


/**
 * @brief Draw the menu label and the optional status icon.
 *
 * The icon (when present) is drawn first with alpha blending, then
 * BMenuItem::DrawContent() handles the text label.
 */
void
StatusMenuItem::DrawContent()
{
	if (fIcon == NULL)
		return BMenuItem::DrawContent();

	// blend transparency
	Menu()->SetDrawingMode(B_OP_ALPHA);
	Menu()->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);

	// draw bitmap
	Menu()->DrawBitmapAsync(fIcon, IconRect().LeftTop());

	BMenuItem::DrawContent();
}


/**
 * @brief Report the desired content size, leaving room for the status icon.
 *
 * @param _width  Out: width reduced by the icon area; may be NULL.
 * @param _height Out: natural label height; may be NULL.
 */
void
StatusMenuItem::GetContentSize(float* _width, float* _height)
{
	float width;
	float height;
	BMenuItem::GetContentSize(&width, &height);

	// prevent label from drawing over icon
	if (_width != NULL)
		*_width = Menu()->Bounds().Width() - IconRect().Width() - Spacing() * 4;

	if (_height != NULL)
		*_height = height;
}


/** @brief Return the currently displayed status icon (may be NULL). */
BBitmap*
StatusMenuItem::Icon()
{
	return fIcon;
}


/**
 * @brief Replace the displayed status icon.
 *
 * Ownership of @a icon stays with the caller; the item only stores the
 * pointer.
 *
 * @param icon New bitmap to draw, or NULL to clear.
 */
void
StatusMenuItem::SetIcon(BBitmap* icon)
{
	fIcon = icon;
}


/** @brief Compute the icon rectangle within the parent menu bounds. */
BRect
StatusMenuItem::IconRect()
{
	BRect bounds(Menu()->Bounds());
	bounds.top += roundf(Spacing() / 2); // center inside menu field vertically
	bounds.right -= Spacing() * 3; // move inside menu field horizontally
	return BLayoutUtils::AlignInFrame(bounds, IconSize(),
		BAlignment(B_ALIGN_RIGHT, B_ALIGN_TOP));
}


/** @brief Pixel size of the mini status icon, scaled by control_look. */
BSize
StatusMenuItem::IconSize()
{
	return be_control_look->ComposeIconSize(B_MINI_ICON);
}


/** @brief Default horizontal spacing between elements, from control_look. */
float
StatusMenuItem::Spacing()
{
	return be_control_look->DefaultLabelSpacing();
}


//	#pragma mark - StatusMenuField


/**
 * @brief Construct a StatusMenuField and load its stop/warn icons.
 *
 * @param label Field label drawn before the menu.
 * @param menu  Menu placed inside the field.
 */
StatusMenuField::StatusMenuField(const char* label, BMenu* menu)
	:
	BMenuField(label, menu),
	fStatus(B_EMPTY_STRING),
	fStopIcon(NULL),
	fWarnIcon(NULL)
{
	_FillIcons();
}


/** @brief Free the cached stop and warning bitmaps. */
StatusMenuField::~StatusMenuField()
{
	delete fStopIcon;
	delete fWarnIcon;
}


/**
 * @brief Toggle the duplicate (stop) status.
 *
 * @param on When true, show the stop icon and set the status string to
 *           @c "duplicate"; when false, clear the icon and status.
 */
void
StatusMenuField::SetDuplicate(bool on)
{
	ShowStopIcon(on);
	on ? SetStatus(kDuplicate) : ClearStatus();
	Invalidate();
}


/**
 * @brief Toggle the unmatched-roles (warning) status.
 *
 * @param on When true, show the warning icon and set the status string to
 *           @c "unmatched"; when false, clear the icon and status.
 */
void
StatusMenuField::SetUnmatched(bool on)
{
	ShowWarnIcon(on);
	on ? SetStatus(kUnmatched) : ClearStatus();
	Invalidate();
}


/**
 * @brief Attach or remove the stop icon on the marked StatusMenuItem.
 *
 * @param show True to attach, false to remove.
 */
void
StatusMenuField::ShowStopIcon(bool show)
{
	// show or hide the stop icon
	StatusMenuItem* item = dynamic_cast<StatusMenuItem*>(MenuItem());
	if (item != NULL)
		item->SetIcon(show ? fStopIcon : NULL);
}


/**
 * @brief Attach or remove the warn icon on the marked StatusMenuItem.
 *
 * @param show True to attach, false to remove.
 */
void
StatusMenuField::ShowWarnIcon(bool show)
{
	// show or hide the warn icon
	StatusMenuItem* item = dynamic_cast<StatusMenuItem*>(MenuItem());
	if (item != NULL)
		item->SetIcon(show ? fWarnIcon : NULL);
}


/** @brief Reset the status string and remove any tooltip. */
void
StatusMenuField::ClearStatus()
{
	fStatus = B_EMPTY_STRING;
	SetToolTip((const char*)NULL);
}


/**
 * @brief Set the status string and refresh the tooltip accordingly.
 *
 * Recognized values are @c "duplicate" and @c "unmatched"; any other
 * string clears the tooltip text.
 *
 * @param status New status token.
 */
void
StatusMenuField::SetStatus(BString status)
{
	fStatus = status;

	const char* tooltip = B_EMPTY_STRING;
	if (fStatus == kDuplicate)
		tooltip = B_TRANSLATE("Error: duplicate keys");
	else if (fStatus == kUnmatched)
		tooltip = B_TRANSLATE("Warning: left and right key roles do not match");

	SetToolTip(tooltip);
}


//	#pragma mark - StatusMenuField private methods


/**
 * @brief Lazily allocate and load the stop and warn icons.
 *
 * Bitmaps are obtained from app_server via @c BIconUtils::GetSystemIcon().
 * On allocation or load failure the corresponding pointer is left NULL,
 * which simply suppresses the badge.
 *
 * @todo Replace the generic dialog-error / dialog-warning icons with
 *       glyphs better matched to keyboard role conflicts.
 */
void
StatusMenuField::_FillIcons()
{
	// fill out the icons with the stop and warn icons from app_server
	// TODO find better icons

	if (fStopIcon == NULL) {
		// allocate the fStopIcon bitmap
		fStopIcon = new (std::nothrow) BBitmap(_IconRect(), 0, B_RGBA32);
		if (fStopIcon == NULL || fStopIcon->InitCheck() != B_OK) {
			FTRACE((stderr, "MKW::_FillIcons() - No memory for stop bitmap\n"));
			delete fStopIcon;
			fStopIcon = NULL;
			return;
		}

		// load dialog-error icon bitmap
		if (BIconUtils::GetSystemIcon("dialog-error", fStopIcon) != B_OK) {
			delete fStopIcon;
			fStopIcon = NULL;
			return;
		}
	}

	if (fWarnIcon == NULL) {
		// allocate the fWarnIcon bitmap
		fWarnIcon = new (std::nothrow) BBitmap(_IconRect(), 0, B_RGBA32);
		if (fWarnIcon == NULL || fWarnIcon->InitCheck() != B_OK) {
			FTRACE((stderr, "MKW::_FillIcons() - No memory for warn bitmap\n"));
			delete fWarnIcon;
			fWarnIcon = NULL;
			return;
		}

		// load dialog-warning icon bitmap
		if (BIconUtils::GetSystemIcon("dialog-warning", fWarnIcon) != B_OK) {
			delete fWarnIcon;
			fWarnIcon = NULL;
			return;
		}
	}
}


/** @brief Default rectangle used when allocating the status icon bitmaps. */
BRect
StatusMenuField::_IconRect()
{
	BSize iconSize = be_control_look->ComposeIconSize(B_MINI_ICON);
	return BRect(0, 0, iconSize.Width() - 1, iconSize.Height() - 1);
}
