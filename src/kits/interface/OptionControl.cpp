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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2003-2006 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (burton666@libero.it)
 *       Jerome Duval
 */


/**
 * @file OptionControl.cpp
 * @brief Implementation of BOptionControl, an abstract base for option-selection controls
 *
 * BOptionControl is the abstract base class for controls that allow the user to
 * select one option from a set of named choices. Concrete subclasses (BOptionPopUp)
 * implement a specific widget.
 *
 * @see BOptionPopUp, BControl
 */


#include <OptionControl.h>

#include <cstring>


/**
 * @brief Constructs a BOptionControl with an explicit frame rectangle (legacy layout).
 *
 * @param frame   The control's frame rectangle in parent coordinates.
 * @param name    The internal name of the control.
 * @param label   The label string displayed by the control.
 * @param message The message sent when the selected option changes.
 * @param resize  Resizing mode flags passed to BControl.
 * @param flags   View flags passed to BControl.
 *
 * @see BControl::BControl()
 */
BOptionControl::BOptionControl(BRect frame, const char *name, const char *label,
								BMessage *message, uint32 resize, uint32 flags)
	:
	BControl(frame, name, label, message, resize, flags)
{
}


/**
 * @brief Constructs a BOptionControl for use with the layout system.
 *
 * @param name    The internal name of the control.
 * @param label   The label string displayed by the control.
 * @param message The message sent when the selected option changes.
 * @param flags   View flags passed to BControl.
 *
 * @see BControl::BControl()
 */
BOptionControl::BOptionControl(const char *name, const char *label,
								BMessage *message, uint32 flags)
	:
	BControl(name, label, message, flags)
{
}


/**
 * @brief Destroys the BOptionControl.
 *
 * The base class destructor handles cleanup of BControl resources.
 */
BOptionControl::~BOptionControl()
{
}


/**
 * @brief Handles incoming messages, including option-value change notifications.
 *
 * If a @c B_OPTION_CONTROL_VALUE message arrives containing a @c "be:value"
 * int32 field, SetValue() and Invoke() are called to update and broadcast
 * the new selection.  All other messages are forwarded to BControl.
 *
 * @param message The message to handle.
 *
 * @see MakeValueMessage(), SetValue()
 */
void
BOptionControl::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case B_OPTION_CONTROL_VALUE:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				SetValue(value);
				Invoke();
			}
			break;
		}
		default:
			BControl::MessageReceived(message);
			break;
	}
}


/**
 * @brief Appends a new option with the given name and value after all existing options.
 *
 * Convenience wrapper around AddOptionAt() that always inserts at the end.
 *
 * @param name  The display name of the new option.
 * @param value The integer value associated with the option.
 * @return @c B_OK on success, or an error code if the option could not be added.
 *
 * @see AddOptionAt(), CountOptions()
 */
status_t
BOptionControl::AddOption(const char *name, int32 value)
{
	int32 numOptions = CountOptions();
	return AddOptionAt(name, value, numOptions);
}


/**
 * @brief Selects the option whose integer value matches @a value.
 *
 * Iterates all options and calls SetValue() on the first match.
 * This is functionally equivalent to calling SetValue() directly, but
 * explicitly validates that a matching option exists.
 *
 * @param value The integer value of the option to select.
 * @return @c B_OK if a matching option was found and selected,
 *         @c B_ERROR if no option with that value exists.
 *
 * @see SelectOptionFor(const char*), GetOptionAt(), SetValue()
 */
status_t
BOptionControl::SelectOptionFor(int32 value)
{
	// XXX: I wonder why this method was created in the first place,
	// since you can obtain the same result simply by calling SetValue().
	// The only difference I can see is that this method iterates over
	// all the options contained in the control, and then selects the right one.
	int32 numOptions = CountOptions();
	for (int32 c = 0; c < numOptions; c++) {
		const char *name = NULL;
		int32 optionValue;
		if (GetOptionAt(c, &name, &optionValue) && optionValue == value) {
			SetValue(optionValue);
			return B_OK;
		}
	}

	return B_ERROR;
}


/**
 * @brief Selects the option whose display name matches @a name.
 *
 * Performs a case-sensitive string comparison against each option's name.
 *
 * @param name The display name of the option to select.
 * @return @c B_OK if a matching option was found and selected,
 *         @c B_ERROR if no option with that name exists.
 *
 * @see SelectOptionFor(int32), GetOptionAt(), SetValue()
 */
status_t
BOptionControl::SelectOptionFor(const char *name)
{
	int32 numOptions = CountOptions();
	for (int32 c = 0; c < numOptions; c++) {
		const char *optionName = NULL;
		int32 optionValue;
		if (GetOptionAt(c, &optionName, &optionValue)
						&& !strcmp(name, optionName)) {
			SetValue(optionValue);
			return B_OK;
		}
	}
	return B_ERROR;
}


/**
 * @brief Creates a @c B_OPTION_CONTROL_VALUE message carrying the specified integer value.
 *
 * The returned message should be posted to the control to trigger a value change.
 * Ownership of the returned object passes to the caller.
 *
 * @param value The integer value to embed in the message's @c "be:value" field.
 * @return A newly allocated BMessage, or @c NULL if allocation or field
 *         insertion fails.
 *
 * @see MessageReceived(), BOptionPopUp::AddOptionAt()
 */
BMessage *
BOptionControl::MakeValueMessage(int32 value)
{
	BMessage *message = new BMessage(B_OPTION_CONTROL_VALUE);
	if (message->AddInt32("be:value", value) != B_OK) {
		delete message;
		message = NULL;
	}

	return message;
}


// Private unimplemented
BOptionControl::BOptionControl()
	:
	BControl(BRect(), "", "", NULL, 0, 0)
{
}


BOptionControl::BOptionControl(const BOptionControl & clone)
	:
	BControl(BRect(), "", "", NULL, 0, 0)
{
}


BOptionControl &
BOptionControl::operator=(const BOptionControl & clone)
{
	return *this;
}


// FBC
status_t BOptionControl::_Reserved_OptionControl_0(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_1(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_2(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_3(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_4(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_5(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_6(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_7(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_8(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_9(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_10(void *, ...) { return B_ERROR; }
status_t BOptionControl::_Reserved_OptionControl_11(void *, ...) { return B_ERROR; }
