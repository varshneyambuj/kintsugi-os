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
 * MIT License. Copyright 2006-2010, Haiku, Inc.
 * Original authors: Stephan Aßmus, Adrien Destugues, Axel Dörfler,
 *                   Oliver Tappe.
 */

/** @file LanguageListView.h
    @brief BOutlineListView and list-item types used to present the
           available / preferred / formatting-conventions language lists in
           the Locale preflet, with drag-and-drop reordering. */

#ifndef LANGUAGE_LIST_VIEW_H
#define LANGUAGE_LIST_VIEW_H


#include <OutlineListView.h>
#include <StringItem.h>
#include <String.h>


/**
 * @brief List item that carries a language identifier (e.g. "en_US") and
 *        a short language code (e.g. "en") alongside the displayable name.
 */
class LanguageListItem : public BStringItem {
public:
								LanguageListItem(const char* text,
									const char* id, const char* languageCode);
								LanguageListItem(
									const LanguageListItem& other);

			/** @brief Returns the full language identifier. */
			const BString&		ID() const { return fID; }
			/** @brief Returns the short language code. */
			const BString&		Code() const { return fCode; }

	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete = false);

protected:
			void				DrawItemWithTextOffset(BView* owner,
									BRect frame, bool complete,
									float textOffset);

private:
			BString				fID;
			BString				fCode;
};


/**
 * @brief LanguageListItem variant that draws a country flag bitmap to the
 *        left of the label.
 *
 * The flag bitmap is loaded lazily in Update() from the locale roster's
 * country-flag resource set.
 */
class LanguageListItemWithFlag : public LanguageListItem {
public:
								LanguageListItemWithFlag(const char* text,
									const char* id, const char* languageCode,
									const char* countryCode = NULL);
								LanguageListItemWithFlag(
									const LanguageListItemWithFlag& other);
	virtual						~LanguageListItemWithFlag();

	virtual void				Update(BView* owner, const BFont* font);

	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete = false);

private:
			BString				fCountryCode;
			BBitmap*			fIcon;
};


/**
 * @brief BOutlineListView subclass that supports lookup by language ID /
 *        code, customizable delete and drag messages, and drop-target
 *        highlighting for inter-list drag-and-drop.
 */
class LanguageListView : public BOutlineListView {
public:
								LanguageListView(const char* name,
									list_view_type type);
	virtual						~LanguageListView();

			LanguageListItem*	ItemForLanguageID(const char* code,
									int32* _index = NULL) const;
			LanguageListItem*	ItemForLanguageCode(const char* code,
									int32* _index = NULL) const;

			void				SetDeleteMessage(BMessage* message);
			void				SetDragMessage(BMessage* message);
			void				SetGlobalDropTargetIndicator(bool isGlobal);

	virtual	void				Draw(BRect updateRect);
	virtual	bool 				InitiateDrag(BPoint point, int32 index,
									bool wasSelected);
	virtual	void 				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual void				MouseUp(BPoint point);
	virtual	void 				AttachedToWindow();
	virtual	void 				MessageReceived(BMessage* message);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);

private:
			bool				_AcceptsDragMessage(
									const BMessage* message) const;

private:
			int32				fDropIndex;
			BRect				fDropTargetHighlightFrame;
			bool				fGlobalDropTargetIndicator;
			BMessage*			fDeleteMessage;
			BMessage*			fDragMessage;
};


#endif	// LANGUAGE_LIST_VIEW_H
