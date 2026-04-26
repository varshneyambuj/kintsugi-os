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
 *   Copyright 2006-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2014 Haiku, Inc. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       John Scipione, jscipione@gmail.com
 */

/**
 * @file AttributeListView.cpp
 * @brief Implementation of AttributeListView and AttributeItem, the list
 *        view that renders a MIME type's Tracker attribute table together
 *        with its translation tables for type codes and display-as
 *        identifiers.
 */


#include "AttributeListView.h"

#include <stdio.h>

#include <Catalog.h>
#include <ControlLook.h>
#include <Locale.h>
#include <ObjectList.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Attribute ListView"


/**
 * @brief Translation table from a B_*_TYPE type code to a translated
 *        display name shown in attribute editors.
 */
const struct type_map kTypeMap[] = {
	{ B_TRANSLATE("String"),         B_STRING_TYPE },
	{ B_TRANSLATE("Boolean"),        B_BOOL_TYPE   },
	{ B_TRANSLATE("Integer 8 bit"),  B_INT8_TYPE   },
	{ B_TRANSLATE("Integer 16 bit"), B_INT16_TYPE  },
	{ B_TRANSLATE("Integer 32 bit"), B_INT32_TYPE  },
	{ B_TRANSLATE("Integer 64 bit"), B_INT64_TYPE  },
	{ B_TRANSLATE("Float"),          B_FLOAT_TYPE  },
	{ B_TRANSLATE("Double"),         B_DOUBLE_TYPE },
	{ B_TRANSLATE("Time"),           B_TIME_TYPE   },
	{ NULL,                          0             }
};


/**
 * @brief Translation table for Tracker "display_as" identifiers and the
 *        list of type codes each one accepts.
 *
 * @todo  In the future, have a (private) Tracker API that exports these
 *        as well as a nice GUI for them.
 */
const struct display_as_map kDisplayAsMap[] = {
	{ B_TRANSLATE("Default"),	NULL,
		{}
	},
	{ B_TRANSLATE("Checkbox"),	B_TRANSLATE("checkbox"),
		{ B_BOOL_TYPE, B_INT8_TYPE, B_INT16_TYPE, B_INT32_TYPE }
	},
	{ B_TRANSLATE("Duration"),	B_TRANSLATE("duration"),
		{ B_TIME_TYPE, B_INT8_TYPE, B_INT16_TYPE, B_INT32_TYPE, B_INT64_TYPE }
	},
	{ B_TRANSLATE("Rating"),	B_TRANSLATE("rating"),
		{ B_INT8_TYPE, B_INT16_TYPE, B_INT32_TYPE }
	},
	{ NULL,						NULL,
		{}
	}
};


/**
 * @brief Appends ", <name>" to @a string when @a displayAs is a
 *        recognised display-as identifier.
 *
 * The leading "<name>" is the identifier itself (the part before the
 * first colon), allowing patterns like "duration:..." to be displayed
 * without their parameters.
 *
 * @param string     String to extend.
 * @param displayAs  Display-as identifier, possibly with arguments.
 */
static void
add_display_as(BString& string, const char* displayAs)
{
	if (displayAs == NULL || !displayAs[0])
		return;

	BString base(displayAs);
	int32 end = base.FindFirst(':');
	if (end > 0)
		base.Truncate(end);

	for (int32 i = 0; kDisplayAsMap[i].name != NULL; i++) {
		if (base.ICompare(kDisplayAsMap[i].identifier) == 0) {
			string += ", ";
			string += base;
			return;
		}
	}
}


/**
 * @brief Writes a human-readable type description into @a string.
 *
 * Looks @a type up in kTypeMap to find a translated name; otherwise
 * formats the four-character code along with its hexadecimal value
 * (printable bytes verbatim, others replaced with '.').
 *
 * @param string     Output string.
 * @param type       Attribute type code.
 * @param displayAs  Optional Tracker display-as identifier appended in
 *                   parentheses when recognised.
 */
static void
name_for_type(BString& string, type_code type, const char* displayAs)
{
	for (int32 i = 0; kTypeMap[i].name != NULL; i++) {
		if (kTypeMap[i].type == type) {
			string = kTypeMap[i].name;
			add_display_as(string, displayAs);
			return;
		}
	}

	char buffer[32];
	buffer[0] = '\'';
	buffer[1] = 0xff & (type >> 24);
	buffer[2] = 0xff & (type >> 16);
	buffer[3] = 0xff & (type >> 8);
	buffer[4] = 0xff & (type);
	buffer[5] = '\'';
	buffer[6] = 0;
	for (int16 i = 0; i < 4; i++) {
		if (buffer[i] < ' ')
			buffer[i] = '.';
	}

	snprintf(buffer + 6, sizeof(buffer) - 6, " (0x%" B_PRIx32 ")", type);
	string = buffer;
}


/**
 * @brief Builds an AttributeItem from one row of a BMimeType attribute
 *        info message.
 *
 * Fields are read with sane defaults so that an attribute info message
 * missing optional fields still produces a complete item.
 *
 * @param attributes  Message returned by BMimeType::GetAttrInfo().
 * @param index       Row index in the attribute info message.
 * @return            Newly allocated item, or NULL if @a index is past
 *                    the end of the message. Caller takes ownership.
 */
AttributeItem*
create_attribute_item(BMessage& attributes, int32 index)
{
	const char* publicName;
	if (attributes.FindString("attr:public_name", index, &publicName) != B_OK)
		return NULL;

	const char* name;
	if (attributes.FindString("attr:name", index, &name) != B_OK)
		name = "-";

	type_code type;
	if (attributes.FindInt32("attr:type", index, (int32 *)&type) != B_OK)
		type = B_STRING_TYPE;

	const char* displayAs;
	if (attributes.FindString("attr:display_as", index, &displayAs) != B_OK)
		displayAs = NULL;

	bool editable;
	if (attributes.FindBool("attr:editable", index, &editable) != B_OK)
		editable = false;
	bool visible;
	if (attributes.FindBool("attr:viewable", index, &visible) != B_OK)
		visible = false;

	int32 alignment;
	if (attributes.FindInt32("attr:alignment", index, &alignment) != B_OK)
		alignment = B_ALIGN_LEFT;

	int32 width;
	if (attributes.FindInt32("attr:width", index, &width) != B_OK)
		width = 50;

	return new AttributeItem(name, publicName, type, displayAs, alignment,
		width, visible, editable);
}


//	#pragma mark - AttributeItem


/**
 * @brief Constructs a fully populated attribute item.
 *
 * @param name        Internal attribute name (e.g. "META:title").
 * @param publicName  Translated label shown to the user.
 * @param type        Attribute type code.
 * @param displayAs   Optional Tracker display-as identifier.
 * @param alignment   Column alignment in Tracker views.
 * @param width       Default column width in Tracker.
 * @param visible     Whether Tracker shows this attribute by default.
 * @param editable    Whether Tracker allows in-place editing.
 */
AttributeItem::AttributeItem(const char* name, const char* publicName,
	type_code type, const char* displayAs, int32 alignment,
	int32 width, bool visible, bool editable)
	:
	BStringItem(publicName),
	fName(name),
	fType(type),
	fDisplayAs(displayAs),
	fAlignment(alignment),
	fWidth(width),
	fVisible(visible),
	fEditable(editable)
{
}


/**
 * @brief Constructs an empty placeholder item with sensible defaults.
 *
 * Used as a sentinel when the attribute window saves a "previous selection"
 * snapshot before the list is rebuilt.
 */
AttributeItem::AttributeItem()
	:
	BStringItem(""),
	fType(B_STRING_TYPE),
	fAlignment(B_ALIGN_LEFT),
	fWidth(60),
	fVisible(true),
	fEditable(false)
{
}


/**
 * @brief Copy constructor; defers actual field copy to operator=.
 *
 * @param other  Source item.
 */
AttributeItem::AttributeItem(const AttributeItem& other)
	:
	BStringItem(other.PublicName())
{
	*this = other;
}


/**
 * @brief Destructor; no owned heap resources.
 */
AttributeItem::~AttributeItem()
{
}


/**
 * @brief Renders the attribute name on the left half and a translated
 *        type description on the right half of @a frame.
 *
 * Honours the list's selected and enabled state when picking high and
 * low colours so the entry blends into Be's standard list styling.
 *
 * @param owner          List view drawing the item.
 * @param frame          Item rectangle in @a owner's coordinates.
 * @param drawEverything Force a full redraw rather than the optimised
 *                       path.
 */
void
AttributeItem::DrawItem(BView* owner, BRect frame, bool drawEverything)
{
	BStringItem::DrawItem(owner, frame, drawEverything);

	BString type;
	name_for_type(type, fType, fDisplayAs.String());
	const char* typeString = type.String();
	if (typeString == NULL)
		return;

	rgb_color highColor = owner->HighColor();
	rgb_color lowColor = owner->LowColor();

	// set the low color
	if (IsSelected())
		owner->SetLowColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
	else
		owner->SetLowColor(ui_color(B_LIST_BACKGROUND_COLOR));

	// set the high color
	if (!IsEnabled()) {
		rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
		if (textColor.red + textColor.green + textColor.blue > 128 * 3)
			owner->SetHighColor(tint_color(textColor, B_DARKEN_2_TINT));
		else
			owner->SetHighColor(tint_color(textColor, B_LIGHTEN_2_TINT));
	} else {
		if (IsSelected())
			owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
		else
			owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
	}

	// move the pen into position
	owner->MovePenTo(frame.left + frame.Width() / 2.0f
			+ be_control_look->DefaultLabelSpacing(),
		owner->PenLocation().y);

	// draw the type string
	owner->DrawString(typeString);

	// set the high color and low color back to the original
	owner->SetHighColor(highColor);
	owner->SetLowColor(lowColor);
}


/**
 * @brief Assigns all attribute fields from @a other to this item.
 *
 * @param other  Source item.
 * @return       Reference to this item.
 */
AttributeItem&
AttributeItem::operator=(const AttributeItem& other)
{
	SetText(other.PublicName());
	fName = other.Name();
	fType = other.Type();
	fDisplayAs = other.DisplayAs();
	fAlignment = other.Alignment();
	fWidth = other.Width();
	fVisible = other.Visible();
	fEditable = other.Editable();

	return *this;
}


/**
 * @brief Field-wise equality test across every visible attribute.
 *
 * @param other  Item to compare to.
 * @return       True when names, type, display-as, alignment, width,
 *               and flag bits all match.
 */
bool
AttributeItem::operator==(const AttributeItem& other) const
{
	return !strcmp(Name(), other.Name())
		&& !strcmp(PublicName(), other.PublicName())
		&& !strcmp(DisplayAs(), other.DisplayAs())
		&& Type() == other.Type()
		&& Alignment() == other.Alignment()
		&& Width() == other.Width()
		&& Visible() == other.Visible()
		&& Editable() == other.Editable();
}


/**
 * @brief Logical inverse of operator==.
 *
 * @param other  Item to compare to.
 * @return       True when any field differs.
 */
bool
AttributeItem::operator!=(const AttributeItem& other) const
{
	return !(*this == other);
}


//	#pragma mark - AttributeListView


/**
 * @brief Constructs the attribute list view with single-selection and
 *        full-frame redraw policies suitable for the FileTypes panel.
 *
 * @param name  Layout name forwarded to BListView.
 */
AttributeListView::AttributeListView(const char* name)
	:
	BListView(name, B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS)
{
}


/**
 * @brief Destructor; deletes all owned items.
 */
AttributeListView::~AttributeListView()
{
	_DeleteItems();
}


/**
 * @brief Deletes every item in the list and empties the BListView.
 */
void
AttributeListView::_DeleteItems()
{
	for (int32 i = CountItems() - 1; i >= 0; i--)
		delete ItemAt(i);

	MakeEmpty();
}


/**
 * @brief Rebuilds the list to mirror the attribute table of @a type.
 *
 * Saves a snapshot of the previous selection, drops every item, and
 * recreates the list from BMimeType::GetAttrInfo(). When exactly one new
 * attribute appears compared to the previous content the new entry is
 * auto-selected so the user can follow asynchronous database updates.
 *
 * @param type  MIME type to read from. NULL leaves the list empty.
 */
void
AttributeListView::SetTo(BMimeType* type)
{
	AttributeItem selectedItem;
	if (CurrentSelection(0) >= 0)
		selectedItem = *(AttributeItem*)ItemAt(CurrentSelection(0));

	// Remove the current items but remember them for now. Also remember
	// the currently selected item.
	BObjectList<AttributeItem, true> previousItems(CountItems());
	while (AttributeItem* item = (AttributeItem*)RemoveItem((int32)0))
		previousItems.AddItem(item);

	// fill it again

	if (type == NULL)
		return;

	BMessage attributes;
	if (type->GetAttrInfo(&attributes) != B_OK)
		return;

	AttributeItem* item;
	int32 i = 0;
	while ((item = create_attribute_item(attributes, i++)) != NULL)
		AddItem(item);

	// Maybe all the items are the same, except for one item. That
	// attribute probably just got added. We should select it so the user
	// can better follow what's going on. The problem we are solving by
	// doing it this way is that updates to the MIME database are very
	// asynchronous. Most likely we have created the new attribute ourselves,
	// but the notification comes so late, we can't know for sure.
	if (CountItems() == previousItems.CountItems() + 1) {
		// First try to make sure that every previous item is there again.
		bool allPreviousItemsFound = true;
		for (i = previousItems.CountItems() - 1; i >= 0; i--) {
			bool previousItemFound = false;
			for (int32 j = CountItems() - 1; j >= 0; j--) {
				item = (AttributeItem*)ItemAt(j);
				if (*item == *previousItems.ItemAt(i)) {
					previousItemFound = true;
					break;
				}
			}
			if (!previousItemFound) {
				allPreviousItemsFound = false;
				break;
			}
		}
		if (allPreviousItemsFound) {
			for (i = CountItems() - 1; i >= 0; i--) {
				item = (AttributeItem*)ItemAt(i);
				bool foundNewItem = false;
				for (int32 j = previousItems.CountItems() - 1; j >= 0; j--) {
					if (*item != *previousItems.ItemAt(j)) {
						foundNewItem = true;
						break;
					}
				}
				if (foundNewItem) {
					Select(i);
					ScrollToSelection();
					break;
				}
			}
		}
	} else {
		// Try to re-selected a previously selected item, if it's the exact
		// same attribute. This helps not loosing the selection, since changes
		// to the model are followed by completely rebuilding the list all the
		// time.
		for (i = CountItems() - 1; i >= 0; i--) {
			item = (AttributeItem*)ItemAt(i);
			if (*item == selectedItem) {
				Select(i);
				ScrollToSelection();
				break;
			}
		}
	}
}


/**
 * @brief Draws the list contents and a vertical divider down the middle.
 *
 * The divider visually separates the attribute name column from its type
 * description column.
 *
 * @param updateRect  Region requested by the redraw notification.
 */
void
AttributeListView::Draw(BRect updateRect)
{
	BListView::Draw(updateRect);

	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));

	float middle = Bounds().Width() / 2.0f;
	StrokeLine(BPoint(middle, 0.0f), BPoint(middle, Bounds().bottom));
}
