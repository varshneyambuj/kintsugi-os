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
 * MIT License. Copyright 2002-2006, Haiku, Inc.
 * Original authors: Oliver Siebenmarck, Andrew McCall, Michael Wilber.
 */

/** @file TranslatorListView.h
    @brief List view and item types for installed translator add-ons. */

#ifndef TRANSLATOR_LIST_VIEW_H
#define TRANSLATOR_LIST_VIEW_H


#include <ListView.h>
#include <String.h>
#include <TranslationDefs.h>


/**
 * @brief List item that pairs a translator id with its name and MIME
 *        supertype.
 */
class TranslatorItem : public BStringItem {
public:
							TranslatorItem(translator_id id, const char* name);
	virtual					~TranslatorItem();

			/** @brief Returns the translator's BTranslatorRoster id. */
			translator_id	ID() const { return fID; }
			/** @brief Returns the MIME supertype used to group the item. */
			const BString&	Supertype() const { return fSupertype; }

private:
			translator_id	fID;
			BString			fSupertype;
};


/**
 * @brief Drop-aware list view that surfaces translator add-ons.
 *
 * Forwards dropped entries to the application as B_REFS_RECEIVED so the
 * application can install them, and supports sorting items by supertype.
 */
class TranslatorListView : public BListView {
public:
							TranslatorListView(const char* name,
								list_view_type type = B_SINGLE_SELECTION_LIST);
	virtual					~TranslatorListView();

			TranslatorItem*	TranslatorAt(int32 index) const;

	virtual	void			MessageReceived(BMessage* message);
	virtual	void			MouseMoved(BPoint point, uint32 transit,
								const BMessage* dragMessage);

			void			SortItems();
};


#endif	// TRANSLATOR_LIST_VIEW_H
