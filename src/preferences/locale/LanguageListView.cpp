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
 *   Copyright 2006-2010, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Adrien Destugues <pulkomandy@gmail.com>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file LanguageListView.cpp
 * @brief Drag-and-drop enabled outline list view for language selection.
 *
 * Implements the available, preferred, and conventions list views used
 * in the Locale preferences pane. Custom drawing dims disabled
 * languages, optional country flags appear next to entries, and a
 * gradient drop-target indicator gives drag feedback during
 * cross-list drag-and-drop reordering.
 */


#include "LanguageListView.h"

#include <stdio.h>

#include <new>

#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <FormattingConventions.h>
#include <GradientLinear.h>
#include <LocaleRoster.h>
#include <Region.h>
#include <Window.h>


/** @brief Maximum drag-bitmap height before items are truncated and faded. */
#define MAX_DRAG_HEIGHT		200.0
/** @brief Alpha value applied to the drag-bitmap pixels for translucency. */
#define ALPHA				170

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LanguageListView"


/**
 * @brief Constructs a list item carrying a language id and code.
 *
 * @param text          Display label (already in display form).
 * @param id            Locale id such as "en_US".
 * @param languageCode  Bare language code such as "en".
 */
LanguageListItem::LanguageListItem(const char* text, const char* id,
	const char* languageCode)
	:
	BStringItem(text),
	fID(id),
	fCode(languageCode)
{
}


/**
 * @brief Copy constructor producing an independent item with the same metadata.
 *
 * @param other  Source item to copy.
 */
LanguageListItem::LanguageListItem(const LanguageListItem& other)
	:
	BStringItem(other.Text()),
	fID(other.fID),
	fCode(other.fCode)
{
}


/**
 * @brief Renders the item without any text inset.
 *
 * @param owner     List view drawing this item.
 * @param frame     Item frame.
 * @param complete  Whether to repaint the entire frame.
 */
void
LanguageListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	DrawItemWithTextOffset(owner, frame, complete, 0);
}


/**
 * @brief Renders the item with the label shifted right by @a textOffset pixels.
 *
 * Disabled items are drawn dimmed with an "[already chosen]" suffix.
 *
 * @param owner       List view drawing this item.
 * @param frame       Item frame.
 * @param complete    Whether to repaint the entire frame.
 * @param textOffset  Extra horizontal offset before the label, in pixels.
 */
void
LanguageListItem::DrawItemWithTextOffset(BView* owner, BRect frame,
	bool complete, float textOffset)
{
	rgb_color highColor = owner->HighColor();
	rgb_color lowColor = owner->LowColor();

	if (IsSelected() || complete) {
		rgb_color color;
		if (IsSelected())
			color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		else
			color = owner->ViewColor();

		owner->SetHighColor(color);
		owner->SetLowColor(color);
		owner->FillRect(frame);
	} else
		owner->SetLowColor(owner->ViewColor());

	BString text = Text();
	if (!IsEnabled()) {
		rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
		if (textColor.red + textColor.green + textColor.blue > 128 * 3)
			owner->SetHighColor(tint_color(textColor, B_DARKEN_2_TINT));
		else
			owner->SetHighColor(tint_color(textColor, B_LIGHTEN_2_TINT));

		text << "   [" << B_TRANSLATE("already chosen") << "]";
	} else {
		if (IsSelected())
			owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
		else
			owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
	}

	owner->MovePenTo(
		frame.left + be_control_look->DefaultLabelSpacing() + textOffset,
		frame.top + BaselineOffset());
	owner->DrawString(text.String());

	owner->SetHighColor(highColor);
	owner->SetLowColor(lowColor);
}


// #pragma mark -


/**
 * @brief Constructs a list item that also carries a country flag icon.
 *
 * @param text          Display label.
 * @param id            Locale id.
 * @param languageCode  Bare language code.
 * @param countryCode   ISO country code used to load the flag icon.
 */
LanguageListItemWithFlag::LanguageListItemWithFlag(const char* text,
	const char* id, const char* languageCode, const char* countryCode)
	:
	LanguageListItem(text, id, languageCode),
	fCountryCode(countryCode),
	fIcon(NULL)
{
}


/**
 * @brief Copy constructor that clones the embedded flag bitmap.
 *
 * @param other  Source item to copy.
 */
LanguageListItemWithFlag::LanguageListItemWithFlag(
	const LanguageListItemWithFlag& other)
	:
	LanguageListItem(other),
	fCountryCode(other.fCountryCode),
	fIcon(other.fIcon != NULL ? new BBitmap(*other.fIcon) : NULL)
{
}


/**
 * @brief Releases the cached flag bitmap.
 */
LanguageListItemWithFlag::~LanguageListItemWithFlag()
{
	delete fIcon;
}


/**
 * @brief Updates measurements and lazily loads the country flag icon.
 *
 * Allocates a square bitmap whose side equals the item height and
 * asks the locale roster to fill it with the country flag. On
 * failure the icon is dropped and the item falls back to text-only
 * rendering.
 *
 * @param owner  List view that owns this item.
 * @param font   Font used to compute the item height.
 */
void
LanguageListItemWithFlag::Update(BView* owner, const BFont* font)
{
	LanguageListItem::Update(owner, font);

	float iconSize = Height();
	SetWidth(Width() + iconSize + be_control_look->DefaultLabelSpacing());

	if (fCountryCode.IsEmpty())
		return;

	fIcon = new(std::nothrow) BBitmap(BRect(0, 0, iconSize - 1, iconSize - 1),
		B_RGBA32);
	if (fIcon != NULL && BLocaleRoster::Default()->GetFlagIconForCountry(fIcon,
			fCountryCode.String()) != B_OK) {
		delete fIcon;
		fIcon = NULL;
	}
}


/**
 * @brief Renders the item with the country flag drawn before the text.
 *
 * Falls back to text-only drawing when the flag icon failed to load.
 *
 * @param owner     List view drawing this item.
 * @param frame     Item frame.
 * @param complete  Whether to repaint the entire frame.
 */
void
LanguageListItemWithFlag::DrawItem(BView* owner, BRect frame, bool complete)
{
	if (fIcon == NULL || !fIcon->IsValid()) {
		DrawItemWithTextOffset(owner, frame, complete, 0);
		return;
	}

	float iconSize = fIcon->Bounds().Width();
	DrawItemWithTextOffset(owner, frame, complete,
		iconSize + be_control_look->DefaultLabelSpacing());

	BRect iconFrame(frame.left + be_control_look->DefaultLabelSpacing(),
		frame.top,
		frame.left + iconSize - 1 + be_control_look->DefaultLabelSpacing(),
		frame.top + iconSize - 1);
	owner->SetDrawingMode(B_OP_OVER);
	owner->DrawBitmap(fIcon, iconFrame);
	owner->SetDrawingMode(B_OP_COPY);
}


// #pragma mark -


/**
 * @brief Constructs an empty language outline list with drag and delete support disabled.
 *
 * @param name  BView name for the list.
 * @param type  Selection mode (single, multiple, ...).
 */
LanguageListView::LanguageListView(const char* name, list_view_type type)
	:
	BOutlineListView(name, type),
	fDropIndex(-1),
	fDropTargetHighlightFrame(),
	fGlobalDropTargetIndicator(false),
	fDeleteMessage(NULL),
	fDragMessage(NULL)
{
}


/**
 * @brief Destroys the list. Items remain owned by the base BOutlineListView.
 */
LanguageListView::~LanguageListView()
{
}


/**
 * @brief Returns the item whose locale id matches @a id.
 *
 * @param id      Locale id to match (e.g. "en_US").
 * @param _index  Optional output: full-list index of the match.
 * @return        Pointer to the item, or NULL if no match was found.
 */
LanguageListItem*
LanguageListView::ItemForLanguageID(const char* id, int32* _index) const
{
	for (int32 index = 0; index < FullListCountItems(); index++) {
		LanguageListItem* item
			= static_cast<LanguageListItem*>(FullListItemAt(index));

		if (item->ID() == id) {
			if (_index != NULL)
				*_index = index;
			return item;
		}
	}

	return NULL;
}


/**
 * @brief Returns the item whose bare language code matches @a code.
 *
 * @param code    Language code to match (e.g. "en").
 * @param _index  Optional output: full-list index of the match.
 * @return        Pointer to the item, or NULL if no match was found.
 */
LanguageListItem*
LanguageListView::ItemForLanguageCode(const char* code, int32* _index) const
{
	for (int32 index = 0; index < FullListCountItems(); index++) {
		LanguageListItem* item
			= static_cast<LanguageListItem*>(FullListItemAt(index));

		if (item->Code() == code) {
			if (_index != NULL)
				*_index = index;
			return item;
		}
	}

	return NULL;
}


/**
 * @brief Replaces the message posted when the user presses Delete.
 *
 * @param message  Owned by the list after this call; pass NULL to disable.
 */
void
LanguageListView::SetDeleteMessage(BMessage* message)
{
	delete fDeleteMessage;
	fDeleteMessage = message;
}


/**
 * @brief Replaces the template message posted when the user starts a drag.
 *
 * @param message  Owned by the list after this call; pass NULL to disable.
 */
void
LanguageListView::SetDragMessage(BMessage* message)
{
	delete fDragMessage;
	fDragMessage = message;
}


/**
 * @brief Switches between per-row and whole-view drop indicators.
 *
 * @param isGlobal  When true, draw a single indicator around the whole view.
 */
void
LanguageListView::SetGlobalDropTargetIndicator(bool isGlobal)
{
	fGlobalDropTargetIndicator = isGlobal;
}


/**
 * @brief Scrolls to the selected item once the list is hooked into a window.
 */
void
LanguageListView::AttachedToWindow()
{
	BOutlineListView::AttachedToWindow();
	ScrollToSelection();
}


/**
 * @brief Forwards drop messages to the configured Messenger.
 *
 * Augments dropped messages with the drop index and a pointer to
 * this view, then sends them to the BWindow Messenger() so the
 * preferences window can react.
 *
 * @param message  Incoming BMessage.
 */
void
LanguageListView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && _AcceptsDragMessage(message)) {
		// Someone just dropped something on us
		BMessage dragMessage(*message);
		dragMessage.AddInt32("drop_index", fDropIndex);
		dragMessage.AddPointer("drop_target", this);
		Messenger().SendMessage(&dragMessage);
	} else
		BOutlineListView::MessageReceived(message);
}


/**
 * @brief Draws the list and the drop-target indicator gradient on top.
 *
 * For per-row indicators, fills the highlighted frame with a
 * gradient. For whole-view indicators, only the inset border ring is
 * filled, leaving the row contents intact.
 *
 * @param updateRect  Region requested for redraw.
 */
void
LanguageListView::Draw(BRect updateRect)
{
	BOutlineListView::Draw(updateRect);

	if (fDropIndex >= 0 && fDropTargetHighlightFrame.IsValid()) {
		// TODO: decide if drawing of a drop target indicator should be moved
		//       into ControlLook
		BGradientLinear gradient;
		int step = fGlobalDropTargetIndicator ? 64 : 128;
		for (int i = 0; i < 256; i += step)
			gradient.AddColor(i % (step * 2) == 0
				? ViewColor() : ui_color(B_CONTROL_HIGHLIGHT_COLOR), i);
		gradient.AddColor(ViewColor(), 255);
		gradient.SetStart(fDropTargetHighlightFrame.LeftTop());
		gradient.SetEnd(fDropTargetHighlightFrame.RightBottom());
		if (fGlobalDropTargetIndicator) {
			BRegion region(fDropTargetHighlightFrame);
			region.Exclude(fDropTargetHighlightFrame.InsetByCopy(2.0, 2.0));
			ConstrainClippingRegion(&region);
			FillRect(fDropTargetHighlightFrame, gradient);
			ConstrainClippingRegion(NULL);
		} else
			FillRect(fDropTargetHighlightFrame, gradient);
	}
}


/**
 * @brief Builds a fading drag bitmap of the selected items and starts the drag.
 *
 * The drag carries a copy of the configured drag template plus one
 * "index" entry per selected row, plus a pointer to this view. When
 * the selection is taller than the maximum drag bitmap, only the
 * topmost rows are drawn and an alpha fade is applied at the bottom.
 *
 * @param point        Mouse-down position.
 * @param dragIndex    Index under the cursor when the drag began.
 * @param wasSelected  Unused.
 * @return             true if a drag was successfully started.
 */
bool
LanguageListView::InitiateDrag(BPoint point, int32 dragIndex,
	bool /*wasSelected*/)
{
	if (fDragMessage == NULL)
		return false;

	BListItem* item = ItemAt(CurrentSelection(0));
	if (item == NULL) {
		// workaround for a timing problem
		// TODO: this should support extending the selection
		item = ItemAt(dragIndex);
		Select(dragIndex);
	}
	if (item == NULL)
		return false;

	// create drag message
	BMessage message = *fDragMessage;
	message.AddPointer("listview", this);

	for (int32 i = 0;; i++) {
		int32 index = CurrentSelection(i);
		if (index < 0)
			break;

		message.AddInt32("index", index);
	}

	// figure out drag rect

	BRect dragRect(0.0, 0.0, Bounds().Width(), -1.0);
	int32 numItems = 0;
	bool fade = false;

	// figure out, how many items fit into our bitmap
	for (int32 i = 0, index; message.FindInt32("index", i, &index) == B_OK;
			i++) {
		BListItem* item = ItemAt(index);
		if (item == NULL)
			break;

		dragRect.bottom += ceilf(item->Height()) + 1.0;
		numItems++;

		if (dragRect.Height() > MAX_DRAG_HEIGHT) {
			dragRect.bottom = MAX_DRAG_HEIGHT;
			fade = true;
			break;
		}
	}

	BBitmap* dragBitmap = new BBitmap(dragRect, B_RGB32, true);
	if (dragBitmap->IsValid()) {
		BView* view = new BView(dragBitmap->Bounds(), "helper", B_FOLLOW_NONE,
			B_WILL_DRAW);
		dragBitmap->AddChild(view);
		dragBitmap->Lock();
		BRect itemBounds(dragRect) ;
		itemBounds.bottom = 0.0;
		// let all selected items, that fit into our drag_bitmap, draw
		for (int32 i = 0; i < numItems; i++) {
			int32 index = message.FindInt32("index", i);
			LanguageListItem* item
				= static_cast<LanguageListItem*>(ItemAt(index));
			itemBounds.bottom = itemBounds.top + ceilf(item->Height());
			if (itemBounds.bottom > dragRect.bottom)
				itemBounds.bottom = dragRect.bottom;
			item->DrawItem(view, itemBounds);
			itemBounds.top = itemBounds.bottom + 1.0;
		}
		// make a black frame around the edge
		view->SetHighColor(0, 0, 0, 255);
		view->StrokeRect(view->Bounds());
		view->Sync();

		uint8* bits = (uint8*)dragBitmap->Bits();
		int32 height = (int32)dragBitmap->Bounds().Height() + 1;
		int32 width = (int32)dragBitmap->Bounds().Width() + 1;
		int32 bpr = dragBitmap->BytesPerRow();

		if (fade) {
			for (int32 y = 0; y < height - ALPHA / 2; y++, bits += bpr) {
				uint8* line = bits + 3;
				for (uint8* end = line + 4 * width; line < end; line += 4)
					*line = ALPHA;
			}
			for (int32 y = height - ALPHA / 2; y < height;
				y++, bits += bpr) {
				uint8* line = bits + 3;
				for (uint8* end = line + 4 * width; line < end; line += 4)
					*line = (height - y) << 1;
			}
		} else {
			for (int32 y = 0; y < height; y++, bits += bpr) {
				uint8* line = bits + 3;
				for (uint8* end = line + 4 * width; line < end; line += 4)
					*line = ALPHA;
			}
		}
		dragBitmap->Unlock();
	} else {
		delete dragBitmap;
		dragBitmap = NULL;
	}

	if (dragBitmap != NULL)
		DragMessage(&message, dragBitmap, B_OP_ALPHA, BPoint(0.0, 0.0));
	else
		DragMessage(&message, dragRect.OffsetToCopy(point), this);

	return true;
}


/**
 * @brief Updates the drop-target highlight as the cursor moves with a drag in flight.
 *
 * Computes the row beneath the cursor (offset by half a row to make
 * the gap between rows feel like a target), invalidates only the
 * region whose highlight changed, then forwards the event to the
 * base class for normal hover handling.
 *
 * @param where        Cursor position in view coordinates.
 * @param transit      Standard BView transit code.
 * @param dragMessage  Drag in flight, or NULL for plain hover.
 */
void
LanguageListView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (dragMessage != NULL && _AcceptsDragMessage(dragMessage)) {
		switch (transit) {
			case B_ENTERED_VIEW:
			case B_INSIDE_VIEW:
			{
				BRect highlightFrame;

				if (fGlobalDropTargetIndicator) {
					highlightFrame = Bounds();
					fDropIndex = 0;
				} else {
					// offset where by half of item height
					BRect r = ItemFrame(0);
					where.y += r.Height() / 2.0;

					int32 index = IndexOf(where);
					if (index < 0)
						index = CountItems();
					highlightFrame = ItemFrame(index);
					if (highlightFrame.IsValid())
						highlightFrame.bottom = highlightFrame.top;
					else {
						highlightFrame = ItemFrame(index - 1);
						if (highlightFrame.IsValid())
							highlightFrame.top = highlightFrame.bottom;
						else {
							// empty view, show indicator at top
							highlightFrame = Bounds();
							highlightFrame.bottom = highlightFrame.top;
						}
					}
					fDropIndex = index;
				}

				if (fDropTargetHighlightFrame != highlightFrame) {
					Invalidate(fDropTargetHighlightFrame);
					fDropTargetHighlightFrame = highlightFrame;
					Invalidate(fDropTargetHighlightFrame);
				}

				BOutlineListView::MouseMoved(where, transit, dragMessage);
				return;
			}
		}
	}

	if (fDropTargetHighlightFrame.IsValid()) {
		Invalidate(fDropTargetHighlightFrame);
		fDropTargetHighlightFrame = BRect();
	}
	BOutlineListView::MouseMoved(where, transit, dragMessage);
}


/**
 * @brief Clears any leftover drop-target highlight at the end of a click.
 *
 * @param point  Release position.
 */
void
LanguageListView::MouseUp(BPoint point)
{
	BOutlineListView::MouseUp(point);
	if (fDropTargetHighlightFrame.IsValid()) {
		Invalidate(fDropTargetHighlightFrame);
		fDropTargetHighlightFrame = BRect();
	}
}


/**
 * @brief Sends the configured delete message when the user presses Delete.
 *
 * Other keys are passed through to the base outline list.
 *
 * @param bytes     UTF-8 bytes of the pressed key.
 * @param numBytes  Length of @a bytes.
 */
void
LanguageListView::KeyDown(const char* bytes, int32 numBytes)
{
	if (bytes[0] == B_DELETE && fDeleteMessage != NULL) {
		Invoke(fDeleteMessage);
		return;
	}

	BOutlineListView::KeyDown(bytes, numBytes);
}


/**
 * @brief Returns true if @a message originated from a LanguageListView source.
 *
 * @param message  Incoming drag message.
 */
bool
LanguageListView::_AcceptsDragMessage(const BMessage* message) const
{
	LanguageListView* sourceView = NULL;
	return message != NULL
		&& message->FindPointer("listview", (void**)&sourceView) == B_OK;
}
