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
 * MIT License. Copyright 2006-2007, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file AttributeListView.h
 * @brief List view and item type that present the Tracker attribute table
 *        of a MIME type, plus translation helpers for type and display-as
 *        codes.
 */

#ifndef ATTRIBUTE_LIST_VIEW_H
#define ATTRIBUTE_LIST_VIEW_H


#include <ListView.h>
#include <Mime.h>
#include <String.h>


/**
 * @brief List item describing one Tracker attribute on a MIME type
 *        (internal name, public name, type code, alignment, width, flags).
 */
class AttributeItem : public BStringItem {
	public:
		AttributeItem(const char* name, const char* publicName, type_code type,
			const char* displayAs, int32 alignment, int32 width, bool visible,
			bool editable);
		AttributeItem();
		AttributeItem(const AttributeItem& other);
		virtual ~AttributeItem();

		virtual void DrawItem(BView* owner, BRect itemRect,
			bool drawEverything = false);

		const char* Name() const { return fName.String(); }
		const char* PublicName() const { return Text(); }

		type_code Type() const { return fType; }
		const char* DisplayAs() const { return fDisplayAs.String(); }
		int32 Alignment() const { return fAlignment; }
		int32 Width() const { return fWidth; }
		bool Visible() const { return fVisible; }
		bool Editable() const { return fEditable; }

		AttributeItem& operator=(const AttributeItem& other);

		bool operator==(const AttributeItem& other) const;
		bool operator!=(const AttributeItem& other) const;

	private:
		BString		fName;
		type_code	fType;
		BString		fDisplayAs;
		int32		fAlignment;
		int32		fWidth;
		bool		fVisible;
		bool		fEditable;
};

/**
 * @brief List view that displays the attribute table of a MIME type and
 *        owns its AttributeItem objects.
 */
class AttributeListView : public BListView {
	public:
		AttributeListView(const char* name);
		virtual ~AttributeListView();

		void SetTo(BMimeType* type);

		virtual void Draw(BRect updateRect);

	private:
		void _DeleteItems();
};

/**
 * @brief Map from a human-readable type name to a B_*_TYPE type_code.
 */
struct type_map {
	const char*	name;
	type_code	type;
};

extern const struct type_map kTypeMap[];

/**
 * @brief Map from a Tracker display-as identifier (e.g. "duration") to a
 *        translated label and the list of type_codes it accepts.
 */
struct display_as_map {
	const char* name;
	const char* identifier;
	type_code	supported[8];
};

extern const struct display_as_map kDisplayAsMap[];

AttributeItem* create_attribute_item(BMessage& attributes, int32 index);

#endif	// ATTRIBUTE_LIST_VIEW_H
