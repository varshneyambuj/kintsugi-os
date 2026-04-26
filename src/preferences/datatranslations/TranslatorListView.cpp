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
 *   Copyright 2002-2006, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Oliver Siebenmarck
 *       Andrew McCall, mccall@digitalparadise.co.uk
 *       Michael Wilber
 */


/**
 * @file TranslatorListView.cpp
 * @brief Drop-aware list view and item type for installed translators.
 *
 * Provides TranslatorItem (a BStringItem that remembers its translator id
 * and MIME supertype) and TranslatorListView (a BListView that accepts
 * drag-and-drop of translator add-on files and forwards them to the
 * application as B_REFS_RECEIVED messages).
 */


#include "TranslatorListView.h"

#include <string.h>

#include <Application.h>
#include <String.h>
#include <TranslatorRoster.h>


/**
 * @brief Compares two TranslatorItem pointers for BListView::SortItems.
 *
 * Sorts primarily by MIME supertype so items group by category, and breaks
 * ties by case-sensitive name comparison.
 *
 * @param a  Pointer to a TranslatorItem* (BListView passes pointer-to-pointer).
 * @param b  Pointer to a TranslatorItem*.
 * @return   Negative, zero, or positive per strcmp conventions.
 */
static int
compare_items(const void* a, const void* b)
{
	const TranslatorItem* itemA = *(const TranslatorItem**)a;
	const TranslatorItem* itemB = *(const TranslatorItem**)b;

	// Compare by supertype, then by name
	int typeDiff = itemA->Supertype().Compare(itemB->Supertype());
	if (typeDiff != 0)
		return typeDiff;

	return strcmp(itemA->Text(), itemB->Text());
}


//	#pragma mark -


/**
 * @brief Builds a TranslatorItem and discovers its MIME supertype.
 *
 * Walks the translator's output formats and remembers the first non-generic
 * supertype it finds. The check skips "application" unless that is the only
 * supertype available, so generic add-ons fall back to that label.
 *
 * @param id    Translator identifier from BTranslatorRoster.
 * @param name  Display string used by the list view.
 */
TranslatorItem::TranslatorItem(translator_id id, const char* name)
	:
	BStringItem(name),
	fID(id)
{
	static BTranslatorRoster* roster = BTranslatorRoster::Default();

	const translation_format* format;
	int32 count;
	roster->GetOutputFormats(id, &format, &count);

	// Find a supertype to categorize the item in ("application" is too generic,
	// so exclude it unless it's the only one available)
	do {
		fSupertype = format->MIME;
		int32 slash = fSupertype.FindFirst('/');
		fSupertype.Truncate(slash);
	} while (fSupertype == "application" && --count != 0);
}


/**
 * @brief Destructor; no owned resources need explicit cleanup.
 */
TranslatorItem::~TranslatorItem()
{
}


//	#pragma mark -


/**
 * @brief Constructs the list view in single-selection mode.
 *
 * @param name  View name passed to BListView.
 * @param type  Selection type kept for API compatibility; the view always
 *              uses single selection internally.
 */
TranslatorListView::TranslatorListView(const char* name, list_view_type type)
	:
	BListView(name, B_SINGLE_SELECTION_LIST)
{
}


/**
 * @brief Destructor; the framework deletes contained items.
 */
TranslatorListView::~TranslatorListView()
{
}


/**
 * @brief Returns the TranslatorItem at @a index, if any.
 *
 * @param index  Zero-based row index.
 * @return       The TranslatorItem at @a index, or NULL when the row holds
 *               a different item type or @a index is out of range.
 */
TranslatorItem*
TranslatorListView::TranslatorAt(int32 index) const
{
	return dynamic_cast<TranslatorItem*>(ItemAt(index));
}


/**
 * @brief Forwards file drops to the application as B_REFS_RECEIVED.
 *
 * Translates incoming B_SIMPLE_DATA messages that carry "refs" entries into
 * a B_REFS_RECEIVED message and posts it to the application, which performs
 * the actual translator install.
 *
 * @param message  Incoming BMessage; only B_SIMPLE_DATA is special-cased,
 *                 everything else is delegated to BListView.
 */
void
TranslatorListView::MessageReceived(BMessage* message)
{
	uint32 type; 
	int32 count;

	switch (message->what) {
		case B_SIMPLE_DATA:
			// Tell the application object that a
			// file has been dropped on this view
			message->GetInfo("refs", &type, &count); 
			if (count > 0 && type == B_REF_TYPE) {
				message->what = B_REFS_RECEIVED;
				be_app->PostMessage(message);
				Invalidate();
			}
			break;

		default:
			BListView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Highlights the view with a red border while a drag hovers over it.
 *
 * @param point        Cursor position in view coordinates.
 * @param transit      One of B_ENTERED_VIEW, B_EXITED_VIEW, B_INSIDE_VIEW.
 * @param dragMessage  The drag payload, or NULL when no drag is active.
 */
void
TranslatorListView::MouseMoved(BPoint point, uint32 transit,
	const BMessage* dragMessage)
{
	if (dragMessage != NULL && transit == B_ENTERED_VIEW) {
		// Draw a red box around the inside of the view
		// to tell the user that this view accepts drops
		SetHighColor(220, 0, 0);
		SetPenSize(4);
		StrokeRect(Bounds());
		SetHighColor(0, 0, 0);
	} else if (dragMessage != NULL && transit == B_EXITED_VIEW)
		Invalidate();

	BListView::MouseMoved(point, transit, dragMessage);
}


/**
 * @brief Sorts items in place using the supertype-then-name comparator.
 */
void
TranslatorListView::SortItems()
{
	BListView::SortItems(&compare_items);
}

