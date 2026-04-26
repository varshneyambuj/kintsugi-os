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
 *   Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MimeTypeListView.cpp
 * @brief Implementation of MimeTypeListView and its row type. Mirrors
 *        the system MIME-type tree, listens to MIME database change
 *        notifications via BMimeType::StartWatching, and handles label
 *        de-duplication so visually identical descriptions get suffixed
 *        with their subtype.
 *
 * @todo  Lazy type collecting (super-types only at startup).
 */


#include "IconView.h"
#include "MimeTypeListView.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <MessageRunner.h>

#include <strings.h>


/** @brief Internal delayed-add message used to debounce MIME type creation. */
const uint32 kMsgAddType = 'adtp';


/**
 * @brief Returns true when @a type is the application signature of
 *        itself, which is the convention an installed application
 *        signature follows.
 *
 * Exported because both the list view and the editors need to filter
 * MIME types into "regular" and "application signature" buckets.
 *
 * @param type  MIME type to test.
 * @return      True for application signatures, false otherwise.
 */
bool
mimetype_is_application_signature(BMimeType& type)
{
	char preferredApp[B_MIME_TYPE_LENGTH];

	// The preferred application of an application is the same
	// as its signature.

	return type.GetPreferredApp(preferredApp) == B_OK
		&& !strcasecmp(type.Type(), preferredApp);
}


//	#pragma mark -


/**
 * @brief Constructs an item that mirrors a BMimeType.
 *
 * @param type      Source MIME type; only its identifier and supertype
 *                  state are captured at construction time.
 * @param showIcon  Render the type's mini icon next to the label.
 * @param flat      When true, render at outline level 0 regardless of
 *                  whether the type has a supertype (used by flat list
 *                  views like the chooser dialog).
 */
MimeTypeItem::MimeTypeItem(BMimeType& type, bool showIcon, bool flat)
	: BStringItem(type.Type(), !flat && !type.IsSupertypeOnly() ? 1 : 0, false),
	fType(type.Type()),
	fFlat(flat),
	fShowIcon(showIcon)
{
	_SetTo(type);
}


/**
 * @brief Constructs an item from a raw MIME identifier string.
 *
 * @param type      MIME identifier such as "text/plain" or "image".
 * @param showIcon  Render the type's mini icon next to the label.
 * @param flat      When true, render at outline level 0 regardless of
 *                  the slash in @a type.
 */
MimeTypeItem::MimeTypeItem(const char* type, bool showIcon, bool flat)
	: BStringItem(type, !flat && strchr(type, '/') != NULL ? 1 : 0, false),
	fType(type),
	fFlat(flat),
	fShowIcon(showIcon)
{
	BMimeType mimeType(type);
	_SetTo(mimeType);
}


/**
 * @brief Destructor; no owned heap resources.
 */
MimeTypeItem::~MimeTypeItem()
{
}


/**
 * @brief Draws the row, optionally rendering the type's mini icon and
 *        bolding super-type rows.
 *
 * Falls back to a generic file or application icon when the MIME type
 * has no custom icon installed.
 *
 * @param owner    List view owning this item.
 * @param frame    Item rectangle in the owner's coordinates.
 * @param complete Force a full row redraw rather than the optimised path.
 */
void
MimeTypeItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	BFont font;

	if (IsSupertypeOnly()) {
		owner->GetFont(&font);
		BFont boldFont(font);
		boldFont.SetFace(B_BOLD_FACE);
		owner->SetFont(&boldFont);
	}

	BRect rect = frame;
	if (fFlat) {
		// This is where the latch would be - yet can freely consider this
		// as an ugly hack
		rect.left -= 11.0f;
	}

	if (fShowIcon) {
		rgb_color lowColor = owner->LowColor();

		if (IsSelected() || complete) {
			if (IsSelected())
				owner->SetLowColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));

			owner->FillRect(rect, B_SOLID_LOW);
		}

		const BRect iconRect(BPoint(0, 0), be_control_look->ComposeIconSize(B_MINI_ICON));
		BBitmap bitmap(iconRect, B_RGBA32);
		BMimeType mimeType(fType.String());
		status_t status = icon_for_type(mimeType, bitmap, B_MINI_ICON);
		if (status < B_OK) {
			// get default generic/application icon
			BMimeType genericType(fApplicationMode
				? B_ELF_APP_MIME_TYPE : B_FILE_MIME_TYPE);
			status = icon_for_type(genericType, bitmap, B_MINI_ICON);
		}

		if (status == B_OK) {
			BPoint point(rect.left + 2.0f,
				rect.top + (rect.Height() - iconRect.Height()) / 2.0f);

			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(&bitmap, point);
		}

		owner->SetDrawingMode(B_OP_COPY);

		owner->MovePenTo(rect.left + iconRect.Width() + 8.0f, frame.top + fBaselineOffset);
		owner->DrawString(Text());

		owner->SetLowColor(lowColor);
	} else
		BStringItem::DrawItem(owner, rect, complete);

	if (IsSupertypeOnly())
		owner->SetFont(&font);
}


/**
 * @brief Recomputes the item's preferred size and font baseline offset
 *        when the icon or font changes.
 *
 * @param owner  List view owning this item.
 * @param font   Font used to draw the row.
 */
void
MimeTypeItem::Update(BView* owner, const BFont* font)
{
	BStringItem::Update(owner, font);

	if (fShowIcon) {
		const BSize iconSize = be_control_look->ComposeIconSize(B_MINI_ICON);
		SetWidth(Width() + iconSize.Width() + 2.0f);

		if (Height() < (iconSize.Height() + 4.0f))
			SetHeight(iconSize.Height() + 4.0f);

		font_height fontHeight;
		font->GetHeight(&fontHeight);

		fBaselineOffset = fontHeight.ascent
			+ (Height() - ceilf(fontHeight.ascent + fontHeight.descent)) / 2.0f;
	}
}


/**
 * @brief Caches super-type, sub-type, and description fields from
 *        @a type.
 *
 * @param type  Source MIME type.
 */
void
MimeTypeItem::_SetTo(BMimeType& type)
{
	fIsSupertype = type.IsSupertypeOnly();

	if (IsSupertypeOnly()) {
		// this is a super type
		fSupertype = type.Type();
		fDescription = type.Type();
		return;
	}

	const char* subType = strchr(type.Type(), '/');
	fSupertype.SetTo(type.Type(), subType - type.Type());
	fSubtype.SetTo(subType + 1);
		// omit the slash

	UpdateText();
}


/**
 * @brief Refreshes the row's display text from the type's short
 *        description (falling back to the subtype name).
 */
void
MimeTypeItem::UpdateText()
{
	if (IsSupertypeOnly())
		return;

	BMimeType type(fType.String());

	char description[B_MIME_TYPE_LENGTH];
	if (type.GetShortDescription(description) == B_OK)
		SetText(description);
	else
		SetText(Subtype());

	fDescription = Text();
}


/**
 * @brief Appends "(subtype)" to the row's text to disambiguate against
 *        another row that has the same description.
 */
void
MimeTypeItem::AddSubtype()
{
	if (fSubtype == Text())
		return;

	BString text = Description();
	text.Append(" (");
	text.Append(fSubtype);
	text.Append(")");

	SetText(text.String());
}


/**
 * @brief Toggles whether the row draws the type's mini icon.
 *
 * @param showIcon  True to enable icon rendering.
 */
void
MimeTypeItem::ShowIcon(bool showIcon)
{
	fShowIcon = showIcon;
}


/**
 * @brief Marks whether the row represents an application signature so
 *        the fallback icon can be chosen accordingly.
 *
 * @param applicationMode  True for application signatures.
 */
void
MimeTypeItem::SetApplicationMode(bool applicationMode)
{
	fApplicationMode = applicationMode;
}


/**
 * @brief Sort comparator that orders items by supertype first, then by
 *        text label.
 *
 * Used to keep the outline view grouped by family.
 *
 * @param a  First item.
 * @param b  Second item.
 * @return   Standard qsort-style result.
 */
/*static*/
int
MimeTypeItem::Compare(const BListItem* a, const BListItem* b)
{
	const MimeTypeItem* typeA = dynamic_cast<const MimeTypeItem*>(a);
	const MimeTypeItem* typeB = dynamic_cast<const MimeTypeItem*>(b);

	if (typeA != NULL && typeB != NULL) {
		int compare = strcasecmp(typeA->Supertype(), typeB->Supertype());
		if (compare != 0)
			return compare;
	}

	const BStringItem* stringA = dynamic_cast<const BStringItem*>(a);
	const BStringItem* stringB = dynamic_cast<const BStringItem*>(b);

	if (stringA != NULL && stringB != NULL)
		return strcasecmp(stringA->Text(), stringB->Text());

	return (int)(a - b);
}


/**
 * @brief Sort comparator used inside the de-duplication pass that
 *        suffixes labels with their subtype.
 *
 * Compares outline level first, then descriptions, then text.
 *
 * @param a  First item.
 * @param b  Second item.
 * @return   Standard qsort-style result.
 */
/*static*/
int
MimeTypeItem::CompareLabels(const BListItem* a, const BListItem* b)
{
	if (a->OutlineLevel() != b->OutlineLevel())
		return a->OutlineLevel() - b->OutlineLevel();

	const MimeTypeItem* typeA = dynamic_cast<const MimeTypeItem*>(a);
	const MimeTypeItem* typeB = dynamic_cast<const MimeTypeItem*>(b);

	if (typeA != NULL && typeB != NULL) {
		int compare = strcasecmp(typeA->Description(), typeB->Description());
		if (compare != 0)
			return compare;
	}

	const BStringItem* stringA = dynamic_cast<const BStringItem*>(a);
	const BStringItem* stringB = dynamic_cast<const BStringItem*>(b);

	if (stringA != NULL && stringB != NULL)
		return strcasecmp(stringA->Text(), stringB->Text());

	return (int)(a - b);
}


//	#pragma mark -


/**
 * @brief Constructs the outline list view.
 *
 * @param name             Layout name forwarded to BOutlineListView.
 * @param supertype        When non-NULL, restrict the listing to this
 *                         super-type and present subtypes flat.
 * @param showIcons        Render mini icons next to the rows.
 * @param applicationMode  When true, list application signatures
 *                         instead of regular MIME types.
 */
MimeTypeListView::MimeTypeListView(const char* name,
		const char* supertype, bool showIcons, bool applicationMode)
	: BOutlineListView(name, B_SINGLE_SELECTION_LIST),
	fSupertype(supertype),
	fShowIcons(showIcons),
	fApplicationMode(applicationMode)
{
}


/**
 * @brief Destructor; the items are torn down by DetachedFromWindow.
 */
MimeTypeListView::~MimeTypeListView()
{
}


/**
 * @brief Adds every installed MIME type whose super-type matches
 *        @a supertype to the list, optionally under @a supertypeItem.
 *
 * Honours @a fApplicationMode to filter application signatures vs.
 * regular MIME types.
 *
 * @param supertype      Super-type identifier to enumerate.
 * @param supertypeItem  Parent item for the resulting children, or NULL
 *                       to add at the top level.
 */
void
MimeTypeListView::_CollectSubtypes(const char* supertype,
	MimeTypeItem* supertypeItem)
{
	BMessage types;
	if (BMimeType::GetInstalledTypes(supertype, &types) != B_OK)
		return;

	const char* type;
	int32 index = 0;
	while (types.FindString("types", index++, &type) == B_OK) {
		BMimeType mimeType(type);

		bool isApp = mimetype_is_application_signature(mimeType);
		if (fApplicationMode ^ isApp)
			continue;

		MimeTypeItem* typeItem = new MimeTypeItem(mimeType, fShowIcons,
			supertypeItem == NULL);
		typeItem->SetApplicationMode(isApp);

		if (supertypeItem != NULL)
			AddUnder(typeItem, supertypeItem);
		else
			AddItem(typeItem);
	}
}


/**
 * @brief Top-level enumeration that builds either the full MIME tree or
 *        a single super-type's flat subtree, then runs label
 *        de-duplication.
 */
void
MimeTypeListView::_CollectTypes()
{
	if (fSupertype.Type() != NULL) {
		// only show MIME types that belong to this supertype
		_CollectSubtypes(fSupertype.Type(), NULL);
	} else {
		BMessage superTypes;
		if (BMimeType::GetInstalledSupertypes(&superTypes) != B_OK)
			return;

		const char* supertype;
		int32 index = 0;
		while (superTypes.FindString("super_types", index++, &supertype)
			== B_OK) {
			MimeTypeItem* supertypeItem = new MimeTypeItem(supertype);
			AddItem(supertypeItem);

			_CollectSubtypes(supertype, supertypeItem);
		}
	}

	_MakeTypesUnique();
}


/**
 * @brief Sorts the list (or a single subtree) and walks it to suffix any
 *        rows whose displayed label collides with their neighbour.
 *
 * Suffixing is delegated to MimeTypeItem::AddSubtype(), which appends
 * the subtype name in parentheses.
 *
 * @param underItem  Subtree root, or NULL to operate on the entire list.
 */
void
MimeTypeListView::_MakeTypesUnique(MimeTypeItem* underItem)
{
	SortItemsUnder(underItem, underItem != NULL, &MimeTypeItem::Compare);

	bool lastItemSame = false;
	MimeTypeItem* last = NULL;

	int32 index = 0;
	uint32 level = 0;
	if (underItem != NULL) {
		index = FullListIndexOf(underItem) + 1;
		level = underItem->OutlineLevel() + 1;
	}

	for (; index < FullListCountItems(); index++) {
		MimeTypeItem* item = dynamic_cast<MimeTypeItem*>(FullListItemAt(index));
		if (item == NULL)
			continue;

		if (item->OutlineLevel() < level) {
			// left sub-tree
			break;
		}

		item->SetText(item->Description());

		if (last == NULL || MimeTypeItem::CompareLabels(last, item)) {
			if (lastItemSame) {
				last->AddSubtype();
				if (Window())
					InvalidateItem(IndexOf(last));
			}

			lastItemSame = false;
			last = item;
			continue;
		}

		lastItemSame = true;
		last->AddSubtype();
		if (Window())
			InvalidateItem(IndexOf(last));
		last = item;
	}

	if (lastItemSame) {
		last->AddSubtype();
		if (Window())
			InvalidateItem(IndexOf(last));
	}
}


/**
 * @brief Adds, removes, or refreshes a row in response to a MIME database
 *        notification for @a type.
 *
 * Filters out types that do not match the current list mode (regular
 * vs. application signature) and re-attaches the new item under the
 * correct super-type. If a previous SelectNewType() request matches
 * @a type, the new item is selected and the pending request is cleared.
 *
 * @param type  MIME identifier whose visibility must be reconciled.
 */
void
MimeTypeListView::_AddNewType(const char* type)
{
	MimeTypeItem* item = FindItem(type);

	BMimeType mimeType(type);
	bool isApp = mimetype_is_application_signature(mimeType);
	if (fApplicationMode ^ isApp || !mimeType.IsInstalled()) {
		if (item != NULL) {
			// type doesn't belong here
			RemoveItem(item);
			delete item;
		}
		return;
	}

	if (item != NULL) {
		// for some reason, the type already exists
		return;
	}

	BMimeType superType;
	MimeTypeItem* superItem = NULL;
	if (mimeType.GetSupertype(&superType) == B_OK)
		superItem = FindItem(superType.Type());

	item = new MimeTypeItem(mimeType, fShowIcons, fSupertype.Type() != NULL);

	if (item->IsSupertypeOnly())
		item->ShowIcon(false);
	item->SetApplicationMode(isApp);

	if (superItem != NULL) {
		AddUnder(item, superItem);
		InvalidateItem(IndexOf(superItem));
			// the super item is not picked up from the class (ie. bug)
	} else
		AddItem(item);

	UpdateItem(item);

	if (!fSelectNewType.ICompare(mimeType.Type())) {
		SelectItem(item);
		fSelectNewType = "";
	}
}


/**
 * @brief Subscribes to MIME database notifications and populates the
 *        list once the view is attached to a window.
 */
void
MimeTypeListView::AttachedToWindow()
{
	BOutlineListView::AttachedToWindow();

	BMimeType::StartWatching(this);
	_CollectTypes();
}


/**
 * @brief Unsubscribes from MIME database notifications and frees every
 *        list item.
 *
 * Items are recreated on the next AttachedToWindow() call so the view
 * always reflects the live database after re-attachment.
 */
void
MimeTypeListView::DetachedFromWindow()
{
	BOutlineListView::DetachedFromWindow();
	BMimeType::StopWatching(this);

	// free all items, they will be retrieved again in AttachedToWindow()

	for (int32 i = FullListCountItems(); i-- > 0;) {
		delete FullListItemAt(i);
	}
}


/**
 * @brief Handles MIME database change notifications and the internal
 *        delayed-add timer.
 *
 * Description, icon, preferred-app, creation, and deletion changes are
 * all reflected on the matching row. New types are added via a 200 ms
 * delayed BMessageRunner so we observe the type after the database has
 * fully committed it.
 *
 * @param message  Incoming BMessage.
 */
void
MimeTypeListView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_META_MIME_CHANGED:
		{
			const char* type;
			int32 which;
			if (message->FindString("be:type", &type) != B_OK
				|| message->FindInt32("be:which", &which) != B_OK)
				break;

			switch (which) {
				case B_SHORT_DESCRIPTION_CHANGED:
				{
					// update label

					MimeTypeItem* item = FindItem(type);
					if (item != NULL)
						UpdateItem(item);
					break;
				}
				case B_MIME_TYPE_CREATED:
				{
					// delay creation of new item a bit, until the type is fully installed

					BMessage addType(kMsgAddType);
					addType.AddString("type", type);

					if (BMessageRunner::StartSending(this, &addType, 200000ULL,
						1) != B_OK) {
						_AddNewType(type);
					}
					break;
				}
				case B_MIME_TYPE_DELETED:
				{
					// delete item
					MimeTypeItem* item = FindItem(type);
					if (item != NULL) {
						RemoveItem(item);
						delete item;
					}
					break;
				}
				case B_PREFERRED_APP_CHANGED:
				{
					// try to add or remove this type (changing the preferred
					// app might change visibility in our list)
					_AddNewType(type);

					// supposed to fall through
				}
				case B_ICON_CHANGED:
				// TODO: take B_ICON_FOR_TYPE_CHANGED into account, too
				{
					MimeTypeItem* item = FindItem(type);
					if (item != NULL && fShowIcons) {
						// refresh item
						InvalidateItem(IndexOf(item));
					}
					break;
				}

				default:
					break;
			}
			break;
		}

		case kMsgAddType:
		{
			const char* type;
			if (message->FindString("type", &type) == B_OK)
				_AddNewType(type);
			break;
		}

		default:
			BOutlineListView::MessageReceived(message);
	}
}


/**
 * @brief Ensures @a type is selected, deferring the selection if the
 *        type has not yet been observed via the MIME watcher.
 *
 * @param type  MIME identifier to select.
 */
void
MimeTypeListView::SelectNewType(const char* type)
{
	if (SelectType(type))
		return;

	fSelectNewType = type;
}


/**
 * @brief Selects the row for @a type if it currently exists in the list.
 *
 * @param type  MIME identifier to look up.
 * @return      True when a matching row was found and selected.
 */
bool
MimeTypeListView::SelectType(const char* type)
{
	MimeTypeItem* item = FindItem(type);
	if (item == NULL)
		return false;

	SelectItem(item);
	return true;
}


/**
 * @brief Expands the chain of super-items, selects @a item, and scrolls
 *        it into view.
 *
 * Passing NULL clears the selection.
 *
 * @param item  Item to make current, or NULL to deselect.
 */
void
MimeTypeListView::SelectItem(MimeTypeItem* item)
{
	if (item == NULL) {
		Select(-1);
		return;
	}

	// Make sure the item is visible

	BListItem* superItem = item;
	while ((superItem = Superitem(superItem)) != NULL) {
		Expand(superItem);
	}

	// Select it, and make it visible

	int32 index = IndexOf(item);
	Select(index);
	ScrollToSelection();
}


/**
 * @brief Linear search through the full list for the row matching
 *        @a type.
 *
 * @param type  MIME identifier; may be NULL.
 * @return      Matching MimeTypeItem, or NULL when not found.
 */
MimeTypeItem*
MimeTypeListView::FindItem(const char* type)
{
	if (type == NULL)
		return NULL;

	for (int32 i = FullListCountItems(); i-- > 0;) {
		MimeTypeItem* item = dynamic_cast<MimeTypeItem*>(FullListItemAt(i));
		if (item == NULL)
			continue;

		if (!strcasecmp(item->Type(), type))
			return item;
	}

	return NULL;
}


/**
 * @brief Refreshes @a item's text, re-runs label de-duplication on its
 *        subtree, and preserves the selection across the move.
 *
 * @param item  Row whose backing MIME type has changed.
 */
void
MimeTypeListView::UpdateItem(MimeTypeItem* item)
{
	int32 selected = -1;
	if (IndexOf(item) == CurrentSelection())
		selected = CurrentSelection();

	item->UpdateText();
	_MakeTypesUnique(dynamic_cast<MimeTypeItem*>(Superitem(item)));

	if (selected != -1) {
		int32 index = IndexOf(item);
		if (index != selected) {
			Select(index);
			ScrollToSelection();
		}
	}
	if (Window())
		InvalidateItem(IndexOf(item));
}


/**
 * @brief Toggles icon rendering on every non-supertype row and forces
 *        a relayout so the scroller picks up the new metrics.
 *
 * @param showIcons  True to render mini icons next to the rows.
 */
void
MimeTypeListView::ShowIcons(bool showIcons)
{
	if (showIcons == fShowIcons)
		return;

	fShowIcons = showIcons;

	if (Window() == NULL)
		return;

	// update items

	BFont font;
	GetFont(&font);

	for (int32 i = FullListCountItems(); i-- > 0;) {
		MimeTypeItem* item = dynamic_cast<MimeTypeItem*>(FullListItemAt(i));
		if (item == NULL)
			continue;

		if (!item->IsSupertypeOnly())
			item->ShowIcon(showIcons);

		item->Update(this, &font);
	}

	FrameResized(Bounds().Width(), Bounds().Height());
		// update scroller

	Invalidate();
}

