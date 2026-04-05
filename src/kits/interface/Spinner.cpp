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
 *   Copyright 2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file Spinner.cpp
 * @brief Implementation of BSpinner, a spinner control for integer values
 *
 * BSpinner extends BAbstractSpinner to display and edit integer values. Users
 * can type a value directly or click the increment/decrement buttons to step
 * through the range.
 *
 * @see BAbstractSpinner, BDecimalSpinner
 */


#include <Spinner.h>

#include <stdint.h>
#include <stdlib.h>

#include <PropertyInfo.h>
#include <String.h>
#include <TextView.h>


/** @brief Scripting property table for BSpinner, exposing MinValue, MaxValue, and Value. */
static property_info sProperties[] = {
	{
		"MaxValue",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the maximum value of the spinner.",
		0,
		{ B_INT32_TYPE }
	},
	{
		"MaxValue",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the maximum value of the spinner.",
		0,
		{ B_INT32_TYPE }
	},

	{
		"MinValue",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the minimum value of the spinner.",
		0,
		{ B_INT32_TYPE }
	},
	{
		"MinValue",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the minimum value of the spinner.",
		0,
		{ B_INT32_TYPE }
	},

	{
		"Value",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the value of the spinner.",
		0,
		{ B_INT32_TYPE }
	},
	{
		"Value",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the value of the spinner.",
		0,
		{ B_INT32_TYPE }
	},

	{ 0 }
};


//	#pragma mark - BSpinner


/**
 * @brief Constructs a BSpinner with an explicit frame rectangle (legacy layout).
 *
 * @param frame        The frame rectangle of the spinner in the parent's coordinate system.
 * @param name         The name of the view, used for identification.
 * @param label        The label string displayed alongside the spinner.
 * @param message      The message sent when the value changes.
 * @param resizingMode The resizing mode flags passed to BView.
 * @param flags        The view flags passed to BView.
 *
 * @see BAbstractSpinner::BAbstractSpinner()
 */
BSpinner::BSpinner(BRect frame, const char* name, const char* label,
	BMessage* message, uint32 resizingMode, uint32 flags)
	:
	BAbstractSpinner(frame, name, label, message, resizingMode, flags)
{
	_InitObject();
}


/**
 * @brief Constructs a BSpinner using the layout-based (layouter) constructor.
 *
 * @param name    The name of the view, used for identification.
 * @param label   The label string displayed alongside the spinner.
 * @param message The message sent when the value changes.
 * @param flags   The view flags passed to BView.
 *
 * @see BAbstractSpinner::BAbstractSpinner()
 */
BSpinner::BSpinner(const char* name, const char* label,
	BMessage* message, uint32 flags)
	:
	BAbstractSpinner(name, label, message, flags)
{
	_InitObject();
}


/**
 * @brief Constructs a BSpinner from an archived BMessage.
 *
 * Restores the minimum and maximum values from the "_min" and "_max" fields.
 * Falls back to INT32_MIN and INT32_MAX respectively if those fields are absent.
 *
 * @param data The archived BMessage to restore state from.
 *
 * @see Instantiate(), Archive()
 */
BSpinner::BSpinner(BMessage* data)
	:
	BAbstractSpinner(data)
{
	_InitObject();

	if (data->FindInt32("_min", &fMinValue) != B_OK)
		fMinValue = INT32_MIN;

	if (data->FindInt32("_max", &fMaxValue) != B_OK)
		fMaxValue = INT32_MAX;
}


/**
 * @brief Destroys the BSpinner.
 */
BSpinner::~BSpinner()
{
}


/**
 * @brief Creates a new BSpinner from an archived BMessage.
 *
 * This is the BArchivable hook used by the instantiation mechanism. Returns
 * NULL if the archive does not match the "Spinner" class name.
 *
 * @param data The archived BMessage to instantiate from.
 * @return A newly allocated BSpinner, or NULL if instantiation failed.
 *
 * @see Archive(), BSpinner(BMessage*)
 */
BArchivable*
BSpinner::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "Spinner"))
		return new BSpinner(data);

	return NULL;
}


/**
 * @brief Archives the BSpinner's state into a BMessage.
 *
 * Stores the class name, minimum value ("_min"), and maximum value ("_max")
 * in addition to everything archived by BAbstractSpinner.
 *
 * @param data The BMessage to archive into.
 * @param deep If true, child views are also archived.
 * @return A status code.
 * @retval B_OK On success.
 *
 * @see Instantiate(), BAbstractSpinner::Archive()
 */
status_t
BSpinner::Archive(BMessage* data, bool deep) const
{
	status_t status = BAbstractSpinner::Archive(data, deep);
	data->AddString("class", "Spinner");

	if (status == B_OK)
		status = data->AddInt32("_min", fMinValue);

	if (status == B_OK)
		status = data->AddInt32("_max", fMaxValue);

	return status;
}


/**
 * @brief Reports the scripting suites and properties supported by BSpinner.
 *
 * Adds the "suite/vnd.Haiku-intenger-spinner" suite identifier and the
 * property list to the message, then delegates to BView.
 *
 * @param message The BMessage to fill with suite and property information.
 * @return The status code from BView::GetSupportedSuites().
 *
 * @see BView::GetSupportedSuites()
 */
status_t
BSpinner::GetSupportedSuites(BMessage* message)
{
	message->AddString("suites", "suite/vnd.Haiku-intenger-spinner");

	BPropertyInfo prop_info(sProperties);
	message->AddFlat("messages", &prop_info);

	return BView::GetSupportedSuites(message);
}


/**
 * @brief Called when the spinner is attached to a window.
 *
 * Re-applies the current value so that the text view and arrow button states
 * are consistent before the view becomes visible.
 *
 * @see BAbstractSpinner::AttachedToWindow(), SetValue()
 */
void
BSpinner::AttachedToWindow()
{
	SetValue(Value());

	BAbstractSpinner::AttachedToWindow();
}


/**
 * @brief Decrements the spinner value by one step.
 *
 * Calls SetValue() with the current value minus one, which will clamp to
 * the minimum and update the text view and button states.
 *
 * @see Increment(), SetValue()
 */
void
BSpinner::Decrement()
{
	SetValue(Value() - 1);
}


/**
 * @brief Increments the spinner value by one step.
 *
 * Calls SetValue() with the current value plus one, which will clamp to
 * the maximum and update the text view and button states.
 *
 * @see Decrement(), SetValue()
 */
void
BSpinner::Increment()
{
	SetValue(Value() + 1);
}


/**
 * @brief Enables or disables the spinner and its increment/decrement buttons.
 *
 * When enabling, the increment button is enabled only if the current value is
 * below the maximum and the decrement button only if above the minimum.
 *
 * @param enable Pass true to enable the control, false to disable it.
 *
 * @see BAbstractSpinner::SetEnabled(), SetIncrementEnabled(), SetDecrementEnabled()
 */
void
BSpinner::SetEnabled(bool enable)
{
	if (IsEnabled() == enable)
		return;

	SetIncrementEnabled(enable && Value() < fMaxValue);
	SetDecrementEnabled(enable && Value() > fMinValue);

	BAbstractSpinner::SetEnabled(enable);
}


/**
 * @brief Sets the minimum allowable value and re-clamps the current value.
 *
 * If the current value is below the new minimum, it is clipped to @a min.
 *
 * @param min The new minimum integer value.
 *
 * @see SetMaxValue(), SetRange(), SetValue()
 */
void
BSpinner::SetMinValue(int32 min)
{
	fMinValue = min;
	SetValue(Value());
}


/**
 * @brief Sets the maximum allowable value and re-clamps the current value.
 *
 * If the current value is above the new maximum, it is clipped to @a max.
 *
 * @param max The new maximum integer value.
 *
 * @see SetMinValue(), SetRange(), SetValue()
 */
void
BSpinner::SetMaxValue(int32 max)
{
	fMaxValue = max;
	SetValue(Value());
}


/**
 * @brief Returns the current minimum and maximum values via output parameters.
 *
 * @param min Set to the current minimum value.
 * @param max Set to the current maximum value.
 *
 * @see SetRange(), SetMinValue(), SetMaxValue()
 */
void
BSpinner::Range(int32* min, int32* max)
{
	*min = fMinValue;
	*max = fMaxValue;
}


/**
 * @brief Convenience method to set the minimum and maximum values in one call.
 *
 * @param min The new minimum integer value.
 * @param max The new maximum integer value.
 *
 * @see SetMinValue(), SetMaxValue(), Range()
 */
void
BSpinner::SetRange(int32 min, int32 max)
{
	SetMinValue(min);
	SetMaxValue(max);
}


/**
 * @brief Sets the spinner to the given integer value.
 *
 * The value is clamped to [fMinValue, fMaxValue], the text view is updated,
 * the increment and decrement buttons are enabled or disabled as appropriate,
 * and if the value actually changed, BControl::SetValue(), ValueChanged(),
 * Invoke(), and Invalidate() are called.
 *
 * @param value The new value to display.
 *
 * @see Value(), SetMinValue(), SetMaxValue(), BControl::SetValue()
 */
void
BSpinner::SetValue(int32 value)
{
	// clip to range
	if (value < fMinValue)
		value = fMinValue;
	else if (value > fMaxValue)
		value = fMaxValue;

	// update the text view
	BString valueString;
	valueString << value;
	TextView()->SetText(valueString.String());

	// update the up and down arrows
	SetIncrementEnabled(IsEnabled() && value < fMaxValue);
	SetDecrementEnabled(IsEnabled() && value > fMinValue);

	if (value == Value())
		return;

	BControl::SetValue(value);
	((int32*)_reserved)[0] = Value();

	ValueChanged();

	Invoke();
	Invalidate();
}


/**
 * @brief Parses the text view's current text and applies it as the spinner value.
 *
 * Converts the raw string from the embedded text view using atol() and passes
 * it to SetValue(), which clamps and validates the result.
 *
 * @see SetValue(), BAbstractSpinner::TextView()
 */
void
BSpinner::SetValueFromText()
{
	SetValue(atol(TextView()->Text()));
}


//	#pragma mark - BSpinner private methods


/**
 * @brief Initializes private fields and configures the embedded text view.
 *
 * Sets fMinValue to INT32_MIN and fMaxValue to INT32_MAX, right-aligns the
 * text view, and disallows all non-numeric characters except the minus sign
 * and decimal digits.
 *
 * @see BSpinner(), BAbstractSpinner::TextView()
 */
void
BSpinner::_InitObject()
{
	fMinValue = INT32_MIN;
	fMaxValue = INT32_MAX;

	TextView()->SetAlignment(B_ALIGN_RIGHT);
	for (uint32 c = 0; c <= 42; c++)
		TextView()->DisallowChar(c);

	TextView()->DisallowChar(',');

	for (uint32 c = 46; c <= 47; c++)
		TextView()->DisallowChar(c);

	for (uint32 c = 58; c <= 127; c++)
		TextView()->DisallowChar(c);
}


// FBC padding

void BSpinner::_ReservedSpinner20() {}
void BSpinner::_ReservedSpinner19() {}
void BSpinner::_ReservedSpinner18() {}
void BSpinner::_ReservedSpinner17() {}
void BSpinner::_ReservedSpinner16() {}
void BSpinner::_ReservedSpinner15() {}
void BSpinner::_ReservedSpinner14() {}
void BSpinner::_ReservedSpinner13() {}
void BSpinner::_ReservedSpinner12() {}
void BSpinner::_ReservedSpinner11() {}
void BSpinner::_ReservedSpinner10() {}
void BSpinner::_ReservedSpinner9() {}
void BSpinner::_ReservedSpinner8() {}
void BSpinner::_ReservedSpinner7() {}
void BSpinner::_ReservedSpinner6() {}
void BSpinner::_ReservedSpinner5() {}
void BSpinner::_ReservedSpinner4() {}
void BSpinner::_ReservedSpinner3() {}
void BSpinner::_ReservedSpinner2() {}
void BSpinner::_ReservedSpinner1() {}
