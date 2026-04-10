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
 *   Copyright 2003-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini <stefano.ceccherini@gmail.com>
 */


/**
 * @file OptionPopUp.cpp
 * @brief Implementation of BOptionPopUp, a pop-up menu option selector
 *
 * BOptionPopUp implements BOptionControl using a BMenuField-based pop-up menu.
 * Each named option is represented by a BMenuItem; selecting one updates the
 * control's integer value and invokes the associated BMessage.
 *
 * @see BOptionControl, BMenuField, BPopUpMenu
 */


#include <GroupLayout.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <OptionPopUp.h>
#include <PopUpMenu.h>

#include <stdio.h>


/** @brief Extra horizontal padding added between the label and the menu field divider. */
const float kLabelSpace = 8.0;
/** @brief Additional width added to the preferred width to accommodate the pop-up button. */
const float kWidthModifier = 25.0;
/** @brief Additional height added to the preferred height for comfortable click targets. */
const float kHeightModifier = 10.0;


/**
 * @brief Constructs a BOptionPopUp with an explicit frame rectangle (legacy layout).
 *
 * Creates an internal BPopUpMenu and BMenuField that fills the control's bounds.
 *
 * @param frame   The frame rectangle in parent coordinates.
 * @param name    The internal name of the control.
 * @param label   The label string displayed next to the pop-up menu.
 * @param message The message sent when the selected option changes.
 * @param resize  Resizing mode flags passed to BOptionControl.
 * @param flags   View flags passed to BOptionControl.
 *
 * @see BOptionControl::BOptionControl()
 */
BOptionPopUp::BOptionPopUp(BRect frame, const char* name, const char* label,
		BMessage* message, uint32 resize, uint32 flags)
	: BOptionControl(frame, name, label, message, resize, flags)
{
	BPopUpMenu* popUp = new BPopUpMenu(label, true, true);
	fMenuField = new BMenuField(Bounds(), "_menu", label, popUp);
	AddChild(fMenuField);
}


/**
 * @brief Constructs a BOptionPopUp with an explicit frame rectangle and a fixed-size option.
 *
 * Identical to the four-argument frame constructor, but the @a fixed parameter
 * is forwarded to BMenuField to prevent the field from resizing automatically.
 *
 * @param frame   The frame rectangle in parent coordinates.
 * @param name    The internal name of the control.
 * @param label   The label string displayed next to the pop-up menu.
 * @param message The message sent when the selected option changes.
 * @param fixed   If @c true, the BMenuField width is fixed.
 * @param resize  Resizing mode flags passed to BOptionControl.
 * @param flags   View flags passed to BOptionControl.
 *
 * @see BMenuField::BMenuField()
 */
BOptionPopUp::BOptionPopUp(BRect frame, const char* name, const char* label,
		BMessage* message, bool fixed, uint32 resize, uint32 flags)
	: BOptionControl(frame, name, label, message, resize, flags)
{
	BPopUpMenu* popUp = new BPopUpMenu(label, true, true);
	fMenuField = new BMenuField(Bounds(), "_menu", label, popUp, fixed);
	AddChild(fMenuField);
}


/**
 * @brief Constructs a BOptionPopUp for use with the layout system.
 *
 * Installs a BGroupLayout so that the internal BMenuField is positioned
 * correctly by the layout engine.
 *
 * @param name    The internal name of the control.
 * @param label   The label string displayed next to the pop-up menu.
 * @param message The message sent when the selected option changes.
 * @param flags   View flags passed to BOptionControl.
 *
 * @note A BGroupLayout is set on the control to ensure proper layout-driven sizing.
 * @see BOptionControl::BOptionControl()
 */
BOptionPopUp::BOptionPopUp(const char* name, const char* label,
		BMessage* message, uint32 flags)
	: BOptionControl(name, label, message, flags)
{
	// TODO: Is this really needed ? Without this, the view
	// doesn't get layoutted properly
	SetLayout(new BGroupLayout(B_HORIZONTAL));

	BPopUpMenu* popUp = new BPopUpMenu(label, true, true);
	fMenuField = new BMenuField("_menu", label, popUp);
	AddChild(fMenuField);
}


/**
 * @brief Destroys the BOptionPopUp.
 *
 * The BMenuField child is removed and deleted by BView's destructor.
 */
BOptionPopUp::~BOptionPopUp()
{
}


/**
 * @brief Returns a pointer to the internal BMenuField.
 *
 * Callers may use this to adjust the divider or access the underlying BMenu
 * directly, but should not delete the returned object.
 *
 * @return A pointer to the BMenuField used internally; never @c NULL after construction.
 *
 * @see BMenuField
 */
BMenuField*
BOptionPopUp::MenuField()
{
	return fMenuField;
}


/**
 * @brief Returns the name and value of the option at the given index.
 *
 * @param index    Zero-based index of the option to retrieve.
 * @param outName  If non-@c NULL, receives a pointer to the option's label string.
 *                 The string is owned by the BMenuItem and must not be freed.
 * @param outValue If non-@c NULL, receives the integer value stored in the
 *                 item's @c "be:value" message field.
 * @return @c true if the option was found, @c false if @a index is out of range
 *         or the internal menu is @c NULL.
 *
 * @see CountOptions(), AddOptionAt()
 */
bool
BOptionPopUp::GetOptionAt(int32 index, const char** outName, int32* outValue)
{
	bool result = false;
	BMenu* menu = fMenuField->Menu();

	if (menu != NULL) {
		BMenuItem* item = menu->ItemAt(index);
		if (item != NULL) {
			if (outName != NULL)
				*outName = item->Label();
			if (outValue != NULL && item->Message() != NULL)
				item->Message()->FindInt32("be:value", outValue);

			result = true;
		}
	}

	return result;
}


/**
 * @brief Removes and deletes the menu item at the given index.
 *
 * @param index Zero-based index of the option to remove.
 *
 * @see AddOptionAt(), CountOptions()
 */
void
BOptionPopUp::RemoveOptionAt(int32 index)
{
	BMenu* menu = fMenuField->Menu();
	if (menu != NULL)
		delete menu->RemoveItem(index);
}


/**
 * @brief Returns the number of options currently in the control.
 *
 * @return The option count, or 0 if the internal menu is @c NULL.
 *
 * @see AddOptionAt(), RemoveOptionAt()
 */
int32
BOptionPopUp::CountOptions() const
{
	BMenu* menu = fMenuField->Menu();
	return (menu != NULL) ? menu->CountItems() : 0;
}


/**
 * @brief Inserts a new option at the specified index.
 *
 * Creates a BMenuItem whose trigger message embeds @a value under
 * @c "be:value". If this is the first option added, it is immediately
 * selected via SetValue().
 *
 * @param name  The display label for the new option.
 * @param value The integer value associated with the option.
 * @param index The zero-based position at which to insert the option.
 * @return @c B_OK on success.
 * @retval B_BAD_VALUE If @a index is negative or greater than the current count.
 * @retval B_NO_MEMORY If memory allocation for the message or item fails.
 * @retval B_ERROR     If the internal menu is @c NULL.
 *
 * @see RemoveOptionAt(), GetOptionAt(), MakeValueMessage()
 */
status_t
BOptionPopUp::AddOptionAt(const char* name, int32 value, int32 index)
{
	BMenu* menu = fMenuField->Menu();
	if (menu == NULL)
		return B_ERROR;

	int32 numItems = menu->CountItems();
	if (index < 0 || index > numItems)
		return B_BAD_VALUE;

	BMessage* message = MakeValueMessage(value);
	if (message == NULL)
		return B_NO_MEMORY;

	BMenuItem* newItem = new BMenuItem(name, message);
	if (newItem == NULL) {
		delete message;
		return B_NO_MEMORY;
	}

	if (!menu->AddItem(newItem, index)) {
		delete newItem;
		return B_NO_MEMORY;
	}

	newItem->SetTarget(this);

	// We didnt' have any items before, so select the newly added one
	if (numItems == 0)
		SetValue(value);

	return B_OK;
}


/**
 * @brief BeOS R5 compatibility override; delegates to BOptionControl::AllAttached().
 *
 * @note Do not remove; required for binary compatibility with BeOS R5.
 */
void
BOptionPopUp::AllAttached()
{
	BOptionControl::AllAttached();
}


/**
 * @brief Adjusts the menu field divider to match the label width after attachment.
 *
 * Called once the control has been added to a window. Sets the BMenuField
 * divider to the pixel width of the label string plus @c kLabelSpace, and
 * retargets all existing menu items to this control.
 *
 * @see SetLabel(), BMenuField::SetDivider()
 */
void
BOptionPopUp::AttachedToWindow()
{
	BOptionControl::AttachedToWindow();

	BMenu* menu = fMenuField->Menu();
	if (menu != NULL) {
		float labelWidth = fMenuField->StringWidth(fMenuField->Label());
		if (labelWidth > 0.f)
			labelWidth += kLabelSpace;
		fMenuField->SetDivider(labelWidth);
		menu->SetTargetForItems(this);
	}
}


/**
 * @brief Forwards messages to BOptionControl::MessageReceived().
 *
 * @param message The message to handle.
 *
 * @see BOptionControl::MessageReceived()
 */
void
BOptionPopUp::MessageReceived(BMessage* message)
{
	BOptionControl::MessageReceived(message);
}


/**
 * @brief Updates the control label and adjusts the menu field divider accordingly.
 *
 * Sets the label on both the underlying BControl and the BMenuField, then
 * recalculates and applies the divider width so the pop-up button remains
 * properly positioned.
 *
 * @param text The new label string.
 *
 * @see AttachedToWindow(), BMenuField::SetDivider()
 */
void
BOptionPopUp::SetLabel(const char* text)
{
	BControl::SetLabel(text);
	fMenuField->SetLabel(text);
	// We are not sure the menu can keep the whole
	// string as label, so we check against the current label
	float newWidth = fMenuField->StringWidth(fMenuField->Label());
	if (newWidth > 0.f)
		newWidth += kLabelSpace;
	fMenuField->SetDivider(newWidth);
}


/**
 * @brief Sets the control's current value and marks the corresponding menu item.
 *
 * Calls BControl::SetValue() to store the new value, then iterates the pop-up
 * menu to find and mark the item whose @c "be:value" field matches @a value.
 *
 * @param value The integer value of the option to select.
 *
 * @see GetOptionAt(), SelectOptionFor()
 */
void
BOptionPopUp::SetValue(int32 value)
{
	BControl::SetValue(value);
	BMenu* menu = fMenuField->Menu();
	if (menu == NULL)
		return;

	int32 numItems = menu->CountItems();
	for (int32 i = 0; i < numItems; i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item && item->Message()) {
			int32 itemValue;
			item->Message()->FindInt32("be:value", &itemValue);
			if (itemValue == value) {
				item->SetMarked(true);
				break;
			}
		}
	}
}


/**
 * @brief Enables or disables the control and its internal BMenuField.
 *
 * @param state @c true to enable the control, @c false to disable it.
 *
 * @see BControl::SetEnabled(), BMenuField::SetEnabled()
 */
void
BOptionPopUp::SetEnabled(bool state)
{
	BOptionControl::SetEnabled(state);
	if (fMenuField)
		fMenuField->SetEnabled(state);
}


/**
 * @brief Returns the preferred size of the control.
 *
 * Queries the BMenuField for its preferred size, then adjusts height to
 * account for the font metrics and @c kHeightModifier, and adjusts width
 * to include the label string width and @c kWidthModifier.
 *
 * @param _width  If non-@c NULL, receives the preferred width.
 * @param _height If non-@c NULL, receives the preferred height.
 *
 * @see ResizeToPreferred(), BMenuField::GetPreferredSize()
 */
void
BOptionPopUp::GetPreferredSize(float* _width, float* _height)
{
	float width, height;
	fMenuField->GetPreferredSize(&width, &height);

	if (_height != NULL) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		*_height = max_c(height, fontHeight.ascent + fontHeight.descent
			+ fontHeight.leading + kHeightModifier);
	}

	if (_width != NULL) {
		width += fMenuField->StringWidth(BControl::Label())
			+ kLabelSpace + kWidthModifier;
		*_width = width;
	}
}


/**
 * @brief Resizes the control to its preferred size and updates the divider.
 *
 * Calls GetPreferredSize() to obtain the target dimensions, resizes the view,
 * and recalculates the BMenuField divider to match the current label width.
 *
 * @see GetPreferredSize(), BMenuField::SetDivider()
 */
void
BOptionPopUp::ResizeToPreferred()
{
	float width, height;
	GetPreferredSize(&width, &height);
	ResizeTo(width, height);

	float newWidth = fMenuField->StringWidth(BControl::Label());
	fMenuField->SetDivider(newWidth + kLabelSpace);
}


/**
 * @brief Returns the index, name, and value of the currently selected option.
 *
 * Locates the marked item in the internal pop-up menu and fills the optional
 * output parameters.
 *
 * @param outName  If non-@c NULL, receives a pointer to the marked item's label.
 *                 The string is owned by the BMenuItem and must not be freed.
 * @param outValue If non-@c NULL, receives the integer value of the marked item.
 * @return The zero-based index of the selected item, @c -1 if nothing is marked,
 *         or @c B_ERROR if the internal menu is @c NULL.
 *
 * @see SetValue(), GetOptionAt()
 */
int32
BOptionPopUp::SelectedOption(const char** outName, int32* outValue) const
{
	BMenu* menu = fMenuField->Menu();
	if (menu == NULL)
		return B_ERROR;

	BMenuItem* marked = menu->FindMarked();
	if (marked == NULL)
		return -1;

	if (outName != NULL)
		*outName = marked->Label();
	if (outValue != NULL)
		marked->Message()->FindInt32("be:value", outValue);

	return menu->IndexOf(marked);
}


// Private Unimplemented
BOptionPopUp::BOptionPopUp()
	:
	BOptionControl(BRect(), "", "", NULL)
{
}


BOptionPopUp::BOptionPopUp(const BOptionPopUp& clone)
	:
	BOptionControl(clone.Frame(), "", "", clone.Message())
{
}


BOptionPopUp &
BOptionPopUp::operator=(const BOptionPopUp& clone)
{
	return *this;
}


// FBC Stuff
status_t BOptionPopUp::_Reserved_OptionControl_0(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionControl_1(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionControl_2(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionControl_3(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_0(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_1(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_2(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_3(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_4(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_5(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_6(void *, ...) { return B_ERROR; }
status_t BOptionPopUp::_Reserved_OptionPopUp_7(void *, ...) { return B_ERROR; }
