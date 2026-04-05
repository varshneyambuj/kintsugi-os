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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2015-2025 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file DecimalSpinner.cpp
 * @brief Implementation of BDecimalSpinner, a spinner control for floating-point values
 *
 * BDecimalSpinner extends BAbstractSpinner to display and edit decimal (floating-point)
 * values. It supports configurable step sizes and precision formatting.
 *
 * @see BAbstractSpinner, BSpinner
 */


#include <DecimalSpinner.h>

#include <stdio.h>
#include <stdlib.h>

#include <PropertyInfo.h>
#include <TextView.h>


/**
 * @brief Rounds @a value to @a n decimal places using standard rounding.
 *
 * @param value The double-precision value to round.
 * @param n     The number of decimal places to retain.
 * @return The rounded value.
 */
static double
roundTo(double value, uint32 n)
{
	return floor(value * pow(10.0, n) + 0.5) / pow(10.0, n);
}


/** @brief Scripting property table for BDecimalSpinner, exposing MinValue, MaxValue, Precision, Step, and Value. */
static property_info sProperties[] = {
	{
		"MaxValue",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the maximum value of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},
	{
		"MaxValue",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the maximum value of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},

	{
		"MinValue",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the minimum value of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},
	{
		"MinValue",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the minimum value of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},

	{
		"Precision",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the number of decimal places of precision of the spinner.",
		0,
		{ B_UINT32_TYPE }
	},
	{
		"Precision",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the number of decimal places of precision of the spinner.",
		0,
		{ B_UINT32_TYPE }
	},

	{
		"Step",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the step size of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},
	{
		"Step",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the step size of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},

	{
		"Value",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Returns the value of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},
	{
		"Value",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0},
		"Sets the value of the spinner.",
		0,
		{ B_DOUBLE_TYPE }
	},

	{ 0 }
};


//	#pragma mark - BDecimalSpinner


/**
 * @brief Constructs a BDecimalSpinner with an explicit frame rectangle (legacy layout).
 *
 * @param frame        The frame rectangle in the parent view's coordinate system.
 * @param name         The name of the view, used for identification.
 * @param label        The label string displayed alongside the spinner.
 * @param message      The message sent when the value changes.
 * @param resizingMode The resizing mode flags passed to BView.
 * @param flags        The view flags passed to BView.
 *
 * @see BAbstractSpinner::BAbstractSpinner()
 */
BDecimalSpinner::BDecimalSpinner(BRect frame, const char* name,
	const char* label, BMessage* message, uint32 resizingMode, uint32 flags)
	:
	BAbstractSpinner(frame, name, label, message, resizingMode, flags)
{
	_InitObject();
}


/**
 * @brief Constructs a BDecimalSpinner using the layout-based constructor.
 *
 * @param name    The name of the view, used for identification.
 * @param label   The label string displayed alongside the spinner.
 * @param message The message sent when the value changes.
 * @param flags   The view flags passed to BView.
 *
 * @see BAbstractSpinner::BAbstractSpinner()
 */
BDecimalSpinner::BDecimalSpinner(const char* name, const char* label,
	BMessage* message, uint32 flags)
	:
	BAbstractSpinner(name, label, message, flags)
{
	_InitObject();
}


/**
 * @brief Constructs a BDecimalSpinner from an archived BMessage.
 *
 * Restores the minimum value ("_min"), maximum value ("_max"), decimal
 * precision ("_precision"), step size ("_step"), and current value ("_val")
 * from the archive, using sensible defaults if any field is absent.
 *
 * @param data The archived BMessage to restore state from.
 *
 * @see Instantiate(), Archive()
 */
BDecimalSpinner::BDecimalSpinner(BMessage* data)
	:
	BAbstractSpinner(data)
{
	_InitObject();

	if (data->FindDouble("_min", &fMinValue) != B_OK)
		fMinValue = 0.0;

	if (data->FindDouble("_max", &fMaxValue) != B_OK)
		fMaxValue = 100.0;

	if (data->FindUInt32("_precision", &fPrecision) != B_OK)
		fPrecision = 2;

	if (data->FindDouble("_step", &fStep) != B_OK)
		fStep = 1.0;

	if (data->FindDouble("_val", &fValue) != B_OK)
		fValue = 0.0;
}


/**
 * @brief Destroys the BDecimalSpinner and releases the BNumberFormat object.
 */
BDecimalSpinner::~BDecimalSpinner()
{
	delete fNumberFormat;
}


/**
 * @brief Creates a new BDecimalSpinner from an archived BMessage.
 *
 * This is the BArchivable hook used by the instantiation mechanism. Returns
 * NULL if the archive does not match the "DecimalSpinner" class name.
 *
 * @param data The archived BMessage to instantiate from.
 * @return A newly allocated BDecimalSpinner, or NULL if instantiation failed.
 *
 * @see Archive(), BDecimalSpinner(BMessage*)
 */
BArchivable*
BDecimalSpinner::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "DecimalSpinner"))
		return new BDecimalSpinner(data);

	return NULL;
}


/**
 * @brief Archives the BDecimalSpinner's state into a BMessage.
 *
 * Stores the class name together with the minimum value ("_min"), maximum
 * value ("_max"), precision ("_precision"), step size ("_step"), and current
 * value ("_val") in addition to everything archived by BAbstractSpinner.
 *
 * @param data The BMessage to archive into.
 * @param deep If true, child views are also archived.
 * @return A status code.
 * @retval B_OK On success.
 *
 * @see Instantiate(), BAbstractSpinner::Archive()
 */
status_t
BDecimalSpinner::Archive(BMessage* data, bool deep) const
{
	status_t status = BAbstractSpinner::Archive(data, deep);
	data->AddString("class", "DecimalSpinner");

	if (status == B_OK)
		status = data->AddDouble("_min", fMinValue);

	if (status == B_OK)
		status = data->AddDouble("_max", fMaxValue);

	if (status == B_OK)
		status = data->AddUInt32("_precision", fPrecision);

	if (status == B_OK)
		status = data->AddDouble("_step", fStep);

	if (status == B_OK)
		status = data->AddDouble("_val", fValue);

	return status;
}


/**
 * @brief Reports the scripting suites and properties supported by BDecimalSpinner.
 *
 * Adds the "suite/vnd.Haiku-decimal-spinner" suite identifier and the
 * property list to the message, then delegates to BView.
 *
 * @param message The BMessage to fill with suite and property information.
 * @return The status code from BView::GetSupportedSuites().
 *
 * @see BView::GetSupportedSuites()
 */
status_t
BDecimalSpinner::GetSupportedSuites(BMessage* message)
{
	message->AddString("suites", "suite/vnd.Haiku-decimal-spinner");

	BPropertyInfo prop_info(sProperties);
	message->AddFlat("messages", &prop_info);

	return BView::GetSupportedSuites(message);
}


/**
 * @brief Called when the spinner is attached to a window.
 *
 * Re-applies the stored fValue so the text view and arrow button states are
 * consistent before the view becomes visible.
 *
 * @see BAbstractSpinner::AttachedToWindow(), SetValue(double)
 */
void
BDecimalSpinner::AttachedToWindow()
{
	SetValue(fValue);

	BAbstractSpinner::AttachedToWindow();
}


/**
 * @brief Decrements the spinner value by the current step size.
 *
 * Subtracts Step() from the current Value() and passes the result to
 * SetValue(), which clamps and updates the display.
 *
 * @see Increment(), SetValue(double), Step()
 */
void
BDecimalSpinner::Decrement()
{
	SetValue(Value() - Step());
}


/**
 * @brief Increments the spinner value by the current step size.
 *
 * Adds Step() to the current Value() and passes the result to
 * SetValue(), which clamps and updates the display.
 *
 * @see Decrement(), SetValue(double), Step()
 */
void
BDecimalSpinner::Increment()
{
	SetValue(Value() + Step());
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
BDecimalSpinner::SetEnabled(bool enable)
{
	if (IsEnabled() == enable)
		return;

	SetIncrementEnabled(enable && Value() < fMaxValue);
	SetDecrementEnabled(enable && Value() > fMinValue);

	BAbstractSpinner::SetEnabled(enable);
}


/**
 * @brief Sets the number of decimal places displayed and used for rounding.
 *
 * Updates the internal BNumberFormat object to match the new precision.
 * Has no effect if @a precision equals the current precision.
 *
 * @param precision The number of decimal places to display (e.g. 2 for "3.14").
 *
 * @see Precision(), SetValue(double)
 */
void
BDecimalSpinner::SetPrecision(uint32 precision)
{
	if (precision == fPrecision)
		return;

	fPrecision = precision;
	fNumberFormat->SetPrecision(precision);
}


/**
 * @brief Sets the minimum allowable value and re-clamps the current value.
 *
 * If the current value is below the new minimum, it is clipped to @a min.
 *
 * @param min The new minimum double value.
 *
 * @see SetMaxValue(), SetRange(), SetValue(double)
 */
void
BDecimalSpinner::SetMinValue(double min)
{
	fMinValue = min;
	SetValue(Value());
}


/**
 * @brief Sets the maximum allowable value and re-clamps the current value.
 *
 * If the current value is above the new maximum, it is clipped to @a max.
 *
 * @param max The new maximum double value.
 *
 * @see SetMinValue(), SetRange(), SetValue(double)
 */
void
BDecimalSpinner::SetMaxValue(double max)
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
BDecimalSpinner::Range(double* min, double* max)
{
	*min = fMinValue;
	*max = fMaxValue;
}


/**
 * @brief Convenience method to set the minimum and maximum values in one call.
 *
 * @param min The new minimum double value.
 * @param max The new maximum double value.
 *
 * @see SetMinValue(), SetMaxValue(), Range()
 */
void
BDecimalSpinner::SetRange(double min, double max)
{
	SetMinValue(min);
	SetMaxValue(max);
}


/**
 * @brief Sets the spinner value from a 32-bit integer by promoting it to double.
 *
 * This override satisfies the BAbstractSpinner interface; it simply casts
 * @a value to double and delegates to SetValue(double).
 *
 * @param value The integer value to set.
 *
 * @see SetValue(double)
 */
void
BDecimalSpinner::SetValue(int32 value)
{
	SetValue((double)value);
}


/**
 * @brief Sets the spinner to the given double value.
 *
 * The value is clamped to [fMinValue, fMaxValue], formatted by fNumberFormat
 * and written to the text view, and the arrow buttons are updated. If the
 * value actually changed, ValueChanged(), Invoke(), and Invalidate() are called.
 *
 * @param value The new double value to display.
 *
 * @see Value(), SetMinValue(), SetMaxValue(), SetPrecision()
 */
void
BDecimalSpinner::SetValue(double value)
{
	// clip to range
	if (value < fMinValue)
		value = fMinValue;
	else if (value > fMaxValue)
		value = fMaxValue;

	// update the text view
	BString valueString;
	fNumberFormat->Format(valueString, value);

	TextView()->SetText(valueString.String());

	// update the up and down arrows
	SetIncrementEnabled(IsEnabled() && value < fMaxValue);
	SetDecrementEnabled(IsEnabled() && value > fMinValue);

	if (value == fValue)
		return;

	fValue = value;
	ValueChanged();

	Invoke();
	Invalidate();
}


/**
 * @brief Parses the text view's current text and applies it as the spinner value.
 *
 * Uses fNumberFormat to parse the raw string, then rounds to the current
 * precision with roundTo() before passing the result to SetValue(double).
 *
 * @see SetValue(double), Precision(), BAbstractSpinner::TextView()
 */
void
BDecimalSpinner::SetValueFromText()
{
	double parsedValue;
	if (fNumberFormat->Parse(TextView()->Text(), parsedValue) == B_OK)
		SetValue(roundTo(parsedValue, Precision()));
}


//	#pragma mark - BDecimalSpinner private methods


/**
 * @brief Initializes private fields and configures the embedded text view.
 *
 * Sets default values (min 0.0, max 100.0, precision 2, step 1.0, value 0.0),
 * creates the BNumberFormat, right-aligns the text view, and disallows all
 * non-numeric characters except the decimal separator and minus sign.
 *
 * @see BDecimalSpinner(), BAbstractSpinner::TextView()
 */
void
BDecimalSpinner::_InitObject()
{
	fMinValue = 0.0;
	fMaxValue = 100.0;
	fPrecision = 2;
	fStep = 1.0;
	fValue = 0.0;

	fNumberFormat = new BNumberFormat();
	fNumberFormat->SetPrecision(fPrecision);

	TextView()->SetAlignment(B_ALIGN_RIGHT);
	for (uint32 c = 0; c <= 42; c++)
		TextView()->DisallowChar(c);

	TextView()->DisallowChar('/');
	for (uint32 c = 58; c <= 127; c++)
		TextView()->DisallowChar(c);
}


// FBC padding

void BDecimalSpinner::_ReservedDecimalSpinner20() {}
void BDecimalSpinner::_ReservedDecimalSpinner19() {}
void BDecimalSpinner::_ReservedDecimalSpinner18() {}
void BDecimalSpinner::_ReservedDecimalSpinner17() {}
void BDecimalSpinner::_ReservedDecimalSpinner16() {}
void BDecimalSpinner::_ReservedDecimalSpinner15() {}
void BDecimalSpinner::_ReservedDecimalSpinner14() {}
void BDecimalSpinner::_ReservedDecimalSpinner13() {}
void BDecimalSpinner::_ReservedDecimalSpinner12() {}
void BDecimalSpinner::_ReservedDecimalSpinner11() {}
void BDecimalSpinner::_ReservedDecimalSpinner10() {}
void BDecimalSpinner::_ReservedDecimalSpinner9() {}
void BDecimalSpinner::_ReservedDecimalSpinner8() {}
void BDecimalSpinner::_ReservedDecimalSpinner7() {}
void BDecimalSpinner::_ReservedDecimalSpinner6() {}
void BDecimalSpinner::_ReservedDecimalSpinner5() {}
void BDecimalSpinner::_ReservedDecimalSpinner4() {}
void BDecimalSpinner::_ReservedDecimalSpinner3() {}
void BDecimalSpinner::_ReservedDecimalSpinner2() {}
void BDecimalSpinner::_ReservedDecimalSpinner1() {}
