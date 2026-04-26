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
 * MIT License. Copyright 2006, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file MimeTypeListView.h
 * @brief Outline list view rendering the system MIME-type tree, plus the
 *        item type used for both supertype and subtype rows.
 */

#ifndef MIME_TYPE_LIST_VIEW_H
#define MIME_TYPE_LIST_VIEW_H


#include <Mime.h>
#include <OutlineListView.h>
#include <String.h>


/**
 * @brief Outline-list item representing one entry in the MIME database;
 *        carries supertype/subtype split and optional icon rendering.
 */
class MimeTypeItem : public BStringItem {
	public:
		MimeTypeItem(BMimeType& type, bool showIcon = false, bool flat = false);
		MimeTypeItem(const char* type, bool showIcon = false, bool flat = false);
		virtual ~MimeTypeItem();

		virtual void DrawItem(BView* owner, BRect itemRect,
			bool drawEverything = false);
		virtual void Update(BView* owner, const BFont* font);

		const char* Type() const { return fType.String(); }
		const char* Subtype() const { return fSubtype.String(); }
		const char* Supertype() const { return fSupertype.String(); }
		const char* Description() const { return fDescription.String(); }
		bool IsSupertypeOnly() const { return fIsSupertype; }

		void UpdateText();
		void AddSubtype();

		void ShowIcon(bool showIcon);
		void SetApplicationMode(bool applicationMode);

		static int Compare(const BListItem* a, const BListItem* b);
		static int CompareLabels(const BListItem* a, const BListItem* b);

	private:
		void _SetTo(BMimeType& type);

		BString		fSupertype;
		BString		fSubtype;
		BString		fType;
		BString		fDescription;
		float		fBaselineOffset;
		bool		fIsSupertype;
		bool		fFlat;
		bool		fShowIcon;
		bool		fApplicationMode;
};

/**
 * @brief Outline list view that mirrors the system MIME-type tree, kept
 *        in sync with the MIME database via watcher notifications.
 */
class MimeTypeListView : public BOutlineListView {
	public:
		MimeTypeListView(const char* name,
			const char* supertype = NULL, bool showIcons = false,
			bool applicationMode = false);
		virtual ~MimeTypeListView();

		void SelectNewType(const char* type);
		bool SelectType(const char* type);

		void SelectItem(MimeTypeItem* item);
		MimeTypeItem* FindItem(const char* type);

		void UpdateItem(MimeTypeItem* item);

		void ShowIcons(bool showIcons);
		bool IsShowingIcons() const { return fShowIcons; }

	protected:
		virtual void AttachedToWindow();
		virtual void DetachedFromWindow();

		virtual void MessageReceived(BMessage* message);

	private:
		void _CollectSubtypes(const char* supertype, MimeTypeItem* supertypeItem);
		void _CollectTypes();
		void _MakeTypesUnique(MimeTypeItem* underItem = NULL);
		void _AddNewType(const char* type);

		BMimeType	fSupertype;
		BString		fSelectNewType;
		bool		fShowIcons;
		bool		fApplicationMode;
};

extern bool mimetype_is_application_signature(BMimeType& type);

#endif	// MIME_TYPE_LIST_VIEW_H
