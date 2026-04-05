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
 *   Copyright 2001-2009 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ulrich Wimboeck
 *       Marc Flerackers (mflerackers@androme.be)
 *       Rene Gollent
 */


/**
 * @file StringItem.cpp
 * @brief Implementation of BStringItem, a text list item for BListView
 *
 * BStringItem is a concrete BListItem that stores and draws a single text
 * string. It measures its height from the current view font and draws the
 * string with appropriate truncation and selection highlighting.
 *
 * @see BListItem, BListView
 */


#include <StringItem.h>

#include <stdlib.h>
#include <string.h>

#include <ControlLook.h>
#include <Message.h>
#include <View.h>


/**
 * @brief Constructs a BStringItem with the given display text.
 *
 * The text is duplicated internally. The item begins with no measured
 * dimensions (fBaselineOffset is 0); call Update() to measure against
 * a font before the item is drawn.
 *
 * @param text     The string to display; may be NULL to produce an empty item.
 * @param level    The outline nesting level for use in BOutlineListView.
 * @param expanded Whether the item begins in the expanded state.
 *
 * @see BListItem::BListItem(), SetText(), Update()
 */
BStringItem::BStringItem(const char* text, uint32 level, bool expanded)
	:
	BListItem(level, expanded),
	fText(NULL),
	fBaselineOffset(0)
{
	SetText(text);
}


/**
 * @brief Constructs a BStringItem from an archived BMessage.
 *
 * Restores the display text from the "_label" field of the archive.
 *
 * @param archive The BMessage containing the previously archived item state.
 *
 * @see Instantiate(), Archive()
 */
BStringItem::BStringItem(BMessage* archive)
	:
	BListItem(archive),
	fText(NULL),
	fBaselineOffset(0)
{
	const char* string;
	if (archive->FindString("_label", &string) == B_OK)
		SetText(string);
}


/**
 * @brief Destroys the BStringItem and releases the duplicated text buffer.
 */
BStringItem::~BStringItem()
{
	free(fText);
}


/**
 * @brief Creates a new BStringItem from an archived BMessage.
 *
 * This is the BArchivable hook used by the instantiation mechanism. Returns
 * NULL if the archive does not match the "BStringItem" class name.
 *
 * @param archive The archived BMessage to instantiate from.
 * @return A newly allocated BStringItem, or NULL if instantiation failed.
 *
 * @see Archive(), BStringItem(BMessage*)
 */
BArchivable*
BStringItem::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BStringItem"))
		return new BStringItem(archive);

	return NULL;
}


/**
 * @brief Archives the BStringItem's state into a BMessage.
 *
 * Stores the display text in the "_label" field in addition to everything
 * archived by BListItem. The text field is omitted if fText is NULL.
 *
 * @param archive The BMessage to archive into.
 * @param deep    Unused; BStringItem has no child objects to archive.
 * @return A status code.
 * @retval B_OK On success.
 *
 * @see Instantiate(), BListItem::Archive()
 */
status_t
BStringItem::Archive(BMessage* archive, bool deep) const
{
	status_t status = BListItem::Archive(archive);

	if (status == B_OK && fText != NULL)
		status = archive->AddString("_label", fText);

	return status;
}


/**
 * @brief Draws the item's text string within the given frame rectangle.
 *
 * Fills the item background with the selection or view color as appropriate,
 * then draws the text string starting at the label-spacing inset from the left
 * edge of @a frame, positioned at the font baseline. Has no effect if fText
 * is NULL.
 *
 * @param owner    The BView that owns this item and provides the drawing context.
 * @param frame    The bounding rectangle allocated to this item.
 * @param complete If true, the entire background is redrawn even when not selected.
 *
 * @see Update(), BListView::DrawItem()
 */
void
BStringItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	if (fText == NULL)
		return;

	rgb_color lowColor = owner->LowColor();

	if (IsSelected() || complete) {
		rgb_color color;
		if (IsSelected())
			color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		else
			color = owner->ViewColor();

		owner->SetLowColor(color);
		owner->FillRect(frame, B_SOLID_LOW);
	} else
		owner->SetLowColor(owner->ViewColor());

	owner->MovePenTo(frame.left + be_control_look->DefaultLabelSpacing(),
		frame.top + fBaselineOffset);

	owner->DrawString(fText);

	owner->SetLowColor(lowColor);
}


/**
 * @brief Replaces the item's display text.
 *
 * Frees the previous text buffer and duplicates @a text. Passing NULL
 * clears the text, leaving fText as NULL.
 *
 * @param text The new string to display, or NULL to clear it.
 *
 * @see Text(), DrawItem()
 */
void
BStringItem::SetText(const char* text)
{
	free(fText);
	fText = NULL;

	if (text)
		fText = strdup(text);
}


/**
 * @brief Returns the item's current display text.
 *
 * @return A pointer to the internal text buffer, or NULL if no text is set.
 *
 * @see SetText()
 */
const char*
BStringItem::Text() const
{
	return fText;
}


/**
 * @brief Measures and caches the item's width and height for the given font.
 *
 * Sets the item width to the string's pixel width plus the default label
 * spacing, and computes the height from the font's ascent, descent, and
 * leading metrics. Also caches the baseline offset used by DrawItem().
 *
 * @param owner Unused by BStringItem; provided for interface compatibility.
 * @param font  The font to measure against.
 *
 * @see DrawItem(), BListItem::SetWidth(), BListItem::SetHeight()
 */
void
BStringItem::Update(BView* owner, const BFont* font)
{
	if (fText != NULL) {
		SetWidth(font->StringWidth(fText)
			+ be_control_look->DefaultLabelSpacing());
	}

	font_height fheight;
	font->GetHeight(&fheight);

	fBaselineOffset = 2 + ceilf(fheight.ascent + fheight.leading / 2);

	SetHeight(ceilf(fheight.ascent) + ceilf(fheight.descent)
		+ ceilf(fheight.leading) + 4);
}


/**
 * @brief Dispatches a perform code to the base BListItem implementation.
 *
 * This hook exists to support future binary-compatible extensions. Callers
 * should not rely on any specific behavior beyond forwarding to BListItem.
 *
 * @param d   The perform operation code.
 * @param arg An opaque argument whose meaning depends on @a d.
 * @return The status code returned by BListItem::Perform().
 *
 * @see BListItem::Perform()
 */
status_t
BStringItem::Perform(perform_code d, void* arg)
{
	return BListItem::Perform(d, arg);
}


/**
 * @brief Returns the cached baseline offset computed by Update().
 *
 * The baseline offset is the vertical distance from the top of the item's
 * frame to the text baseline, and is used by DrawItem() to position the string.
 *
 * @return The baseline offset in pixels.
 *
 * @see Update(), DrawItem()
 */
float
BStringItem::BaselineOffset() const
{
	return fBaselineOffset;
}


void BStringItem::_ReservedStringItem1() {}
void BStringItem::_ReservedStringItem2() {}


/**
 * @brief Private copy constructor — copying BStringItem is not supported.
 *
 * Declared private and intentionally unimplemented to prevent accidental
 * copying of BStringItem instances.
 */
BStringItem::BStringItem(const BStringItem &)
{
}


/**
 * @brief Private copy-assignment operator — assignment of BStringItem is not supported.
 *
 * Declared private and intentionally unimplemented to prevent accidental
 * assignment of BStringItem instances.
 *
 * @return A reference to this object (never meaningfully used).
 */
BStringItem	&
BStringItem::operator=(const BStringItem &)
{
	return *this;
}
