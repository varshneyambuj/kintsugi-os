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
 *   Copyright 2004-2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       McCall <mccall@@digitalparadise.co.uk>
 *       Mike Berg <mike@berg-net.us>
 *       Julun <host.haiku@gmx.de>
 *       Clemens <mail@Clemens-Zeidler.de>
 *       Adrien Destugues <pulkomandy@pulkomandy.cx>
 *       Hamish Morrison <hamish@lavabit.com>
 */

/** @file DateTimeEdit.cpp
 *  @brief Implements the SectionEdit base class and the TimeEdit / DateEdit
 *         concrete subclasses, which provide locale-aware sectioned editors
 *         for time and date values with up/down arrow controls.
 */


#include "DateTimeEdit.h"

#include <stdlib.h>

#include <ControlLook.h>
#include <DateFormat.h>
#include <LayoutUtils.h>
#include <List.h>
#include <Locale.h>
#include <String.h>
#include <Window.h>


namespace BPrivate {


const uint32 kArrowAreaWidth = 16;


/** @brief Constructs a TimeEdit control.
 *  @param name      Internal view name.
 *  @param sections  Number of time sections (typically 3: hour, minute, second).
 *  @param message   The message sent when the time value changes.
 */
TimeEdit::TimeEdit(const char* name, uint32 sections, BMessage* message)
	:
	SectionEdit(name, sections, message),
	fLastKeyDownTime(0),
	fFields(NULL),
	fFieldCount(0),
	fFieldPositions(NULL),
	fFieldPosCount(0)
{
	InitView();
}


/** @brief Destructor — frees locale field position and field-type buffers. */
TimeEdit::~TimeEdit()
{
	free(fFieldPositions);
	free(fFields);
}


/** @brief Handles a key-down event for numeric digit input.
 *
 *  Accepts digit keys only.  If a digit is pressed within one second of
 *  the previous key press a two-digit value is assembled and validated
 *  against the current section's range.  The display and invocation
 *  message are updated immediately.
 *
 *  @param bytes     Pointer to the key bytes.
 *  @param numBytes  Number of bytes.
 */
void
TimeEdit::KeyDown(const char* bytes, int32 numBytes)
{
	if (IsEnabled() == false)
		return;
	SectionEdit::KeyDown(bytes, numBytes);

	// only accept valid input
	int32 number = atoi(bytes);
	if (number < 0 || bytes[0] < '0')
		return;

	int32 section = FocusIndex();
	if (section < 0 || section > 2)
		return;

	bigtime_t currentTime = system_time();
	if (currentTime - fLastKeyDownTime < 1000000) {
		int32 doubleDigit = number + fLastKeyDownInt * 10;
		if (_IsValidDoubleDigit(doubleDigit))
			number = doubleDigit;
		fLastKeyDownTime = 0;
	} else {
		fLastKeyDownTime = currentTime;
		fLastKeyDownInt = number;
	}

	// update display value
	fHoldValue = number;
	_CheckRange();
	_UpdateFields();

	// send message to change time
	Invoke();
}


/** @brief Initialises the control by reading the current local time. */
void
TimeEdit::InitView()
{
	// make sure we call the base class method, as it
	// will create the arrow bitmaps and the section list
	fTime = BDateTime::CurrentDateTime(B_LOCAL_TIME);
	_UpdateFields();
}


/** @brief Draws a single time section (hour, minute, or second).
 *  @param index   Zero-based section index.
 *  @param bounds  The rectangle allocated to this section.
 *  @param hasFocus True if this section has keyboard focus.
 */
void
TimeEdit::DrawSection(uint32 index, BRect bounds, bool hasFocus)
{
	if (fFieldPositions == NULL || index * 2 + 1 >= (uint32)fFieldPosCount)
		return;

	if (hasFocus)
		SetLowColor(mix_color(ui_color(B_CONTROL_HIGHLIGHT_COLOR),
			ui_color(B_DOCUMENT_BACKGROUND_COLOR), 192));
	else
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	BString field;
	fText.CopyCharsInto(field, fFieldPositions[index * 2],
		fFieldPositions[index * 2 + 1] - fFieldPositions[index * 2]);

	BPoint point(bounds.LeftBottom());
	point.y -= bounds.Height() / 2.0 - 6.0;
	point.x += (bounds.Width() - StringWidth(field)) / 2;
	FillRect(bounds, B_SOLID_LOW);
	DrawString(field, point);
}


/** @brief Draws the separator character between two time sections.
 *  @param index   Zero-based separator index.
 *  @param bounds  The rectangle allocated to this separator.
 */
void
TimeEdit::DrawSeparator(uint32 index, BRect bounds)
{
	if (fFieldPositions == NULL || index * 2 + 2 >= (uint32)fFieldPosCount)
		return;

	BString field;
	fText.CopyCharsInto(field, fFieldPositions[index * 2 + 1],
		fFieldPositions[index * 2 + 2] - fFieldPositions[index * 2 + 1]);

	BPoint point(bounds.LeftBottom());
	point.y -= bounds.Height() / 2.0 - 6.0;
	point.x += (bounds.Width() - StringWidth(field)) / 2;
	DrawString(field, point);
}


/** @brief Returns the width allocated to each separator in pixels.
 *  @return Fixed separator width of 10 pixels.
 */
float
TimeEdit::SeparatorWidth()
{
	return 10.0f;
}


/** @brief Returns the minimum width for a single time section.
 *  @return Width of the string "00" in the plain font.
 */
float
TimeEdit::MinSectionWidth()
{
	return be_plain_font->StringWidth("00");
}


/** @brief Sets focus to the time section at the given index.
 *  @param index  Zero-based section index to focus.
 */
void
TimeEdit::SectionFocus(uint32 index)
{
	fLastKeyDownTime = 0;
	fFocus = index;
	fHoldValue = _SectionValue(index);
	Draw(Bounds());
}


/** @brief Sets the displayed time to the given hour, minute, and second.
 *
 *  If all three components are zero the internal BDateTime is refreshed
 *  from the current local time before applying the new values.
 *
 *  @param hour    Hour value (0-23).
 *  @param minute  Minute value (0-59).
 *  @param second  Second value (0-59).
 */
void
TimeEdit::SetTime(int32 hour, int32 minute, int32 second)
{
	// make sure to update date upon overflow
	if (hour == 0 && minute == 0 && second == 0)
		fTime = BDateTime::CurrentDateTime(B_LOCAL_TIME);

	fTime.SetTime(BTime(hour, minute, second));

	if (LockLooper()) {
		_UpdateFields();
		UnlockLooper();
	}

	Invalidate(Bounds());
}


/** @brief Returns the current time value.
 *  @return A BTime representing the hours, minutes, and seconds currently shown.
 */
BTime
TimeEdit::GetTime()
{
	return fTime.Time();
}


/** @brief Increments the focused time section by one unit. */
void
TimeEdit::DoUpPress()
{
	if (fFocus == -1)
		SectionFocus(0);

	// update displayed value
	fHoldValue += 1;

	_CheckRange();
	_UpdateFields();

	// send message to change time
	Invoke();
}


/** @brief Decrements the focused time section by one unit. */
void
TimeEdit::DoDownPress()
{
	if (fFocus == -1)
		SectionFocus(0);

	// update display value
	fHoldValue -= 1;

	_CheckRange();
	_UpdateFields();

	Invoke();
}


/** @brief Populates the outgoing message with the current hour, minute, and second.
 *  @param message  The message to fill in.
 */
void
TimeEdit::PopulateMessage(BMessage* message)
{
	if (fFocus < 0 || fFocus >= fFieldCount)
		return;

	message->AddBool("time", true);
	message->AddInt32("hour", fTime.Time().Hour());
	message->AddInt32("minute", fTime.Time().Minute());
	message->AddInt32("second", fTime.Time().Second());
}


/** @brief Reformats the display string and refreshes field-position metadata. */
void
TimeEdit::_UpdateFields()
{
	time_t time = fTime.Time_t();

	if (fFieldPositions != NULL) {
		free(fFieldPositions);
		fFieldPositions = NULL;
	}
	fTimeFormat.Format(fText, fFieldPositions, fFieldPosCount, time,
		B_MEDIUM_TIME_FORMAT);

	if (fFields != NULL) {
		free(fFields);
		fFields = NULL;
	}
	fTimeFormat.GetTimeFields(fFields, fFieldCount, B_MEDIUM_TIME_FORMAT);
}


/** @brief Clamps fHoldValue to the valid range for the focused field and applies it. */
void
TimeEdit::_CheckRange()
{
	if (fFocus < 0 || fFocus >= fFieldCount)
		return;

	int32 value = fHoldValue;
	switch (fFields[fFocus]) {
		case B_DATE_ELEMENT_HOUR:
			if (value > 23)
				value = 0;
			else if (value < 0)
				value = 23;

			fTime.SetTime(BTime(value, fTime.Time().Minute(),
				fTime.Time().Second()));
			break;

		case B_DATE_ELEMENT_MINUTE:
			if (value> 59)
				value = 0;
			else if (value < 0)
				value = 59;

			fTime.SetTime(BTime(fTime.Time().Hour(), value,
				fTime.Time().Second()));
			break;

		case B_DATE_ELEMENT_SECOND:
			if (value > 59)
				value = 0;
			else if (value < 0)
				value = 59;

			fTime.SetTime(BTime(fTime.Time().Hour(), fTime.Time().Minute(),
				value));
			break;

		case B_DATE_ELEMENT_AM_PM:
			value = fTime.Time().Hour();
			if (value < 13)
				value += 12;
			else
				value -= 12;
			if (value == 24)
				value = 0;

			// modify hour value to reflect change in am/ pm
			fTime.SetTime(BTime(value, fTime.Time().Minute(),
				fTime.Time().Second()));
			break;

		default:
			return;
	}


	fHoldValue = value;
	Invalidate(Bounds());
}


/** @brief Returns whether a two-digit value is within the focused field's range.
 *  @param value  The candidate two-digit integer.
 *  @return True if \a value is a valid entry for the focused time field.
 */
bool
TimeEdit::_IsValidDoubleDigit(int32 value)
{
	if (fFocus < 0 || fFocus >= fFieldCount)
		return false;

	bool isInRange = false;
	switch (fFields[fFocus]) {
		case B_DATE_ELEMENT_HOUR:
			if (value <= 23)
				isInRange = true;
			break;

		case B_DATE_ELEMENT_MINUTE:
			if (value <= 59)
				isInRange = true;
			break;

		case B_DATE_ELEMENT_SECOND:
			if (value <= 59)
				isInRange = true;
			break;

		default:
			break;
	}

	return isInRange;
}


/** @brief Returns the current numeric value of a time section.
 *  @param index  Zero-based section index.
 *  @return The hour, minute, or second value, or 0 if the index is invalid.
 */
int32
TimeEdit::_SectionValue(int32 index) const
{
	if (index < 0 || index >= fFieldCount)
		return 0;

	int32 value;
	switch (fFields[index]) {
		case B_DATE_ELEMENT_HOUR:
			value = fTime.Time().Hour();
			break;

		case B_DATE_ELEMENT_MINUTE:
			value = fTime.Time().Minute();
			break;

		case B_DATE_ELEMENT_SECOND:
			value = fTime.Time().Second();
			break;

		default:
			value = 0;
			break;
	}

	return value;
}


/** @brief Returns the preferred height for the time editor.
 *  @return Ceiling of 1.4 × (ascent + descent) of the current font.
 */
float
TimeEdit::PreferredHeight()
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	return ceilf((fontHeight.ascent + fontHeight.descent) * 1.4);
}


// #pragma mark -


/** @brief Constructs a DateEdit control.
 *  @param name      Internal view name.
 *  @param sections  Number of date sections (typically 3: day, month, year).
 *  @param message   The message sent when the date value changes.
 */
DateEdit::DateEdit(const char* name, uint32 sections, BMessage* message)
	:
	SectionEdit(name, sections, message),
	fFields(NULL),
	fFieldCount(0),
	fFieldPositions(NULL),
	fFieldPosCount(0)
{
	InitView();
}


/** @brief Destructor — frees locale field position and field-type buffers. */
DateEdit::~DateEdit()
{
	free(fFieldPositions);
	free(fFields);
}


/** @brief Handles a key-down event for numeric digit input.
 *
 *  Accepts digit keys only.  Two-digit combination entry is supported for
 *  day and month fields.  Year entry prefixes with the current century.
 *
 *  @param bytes     Pointer to the key bytes.
 *  @param numBytes  Number of bytes.
 */
void
DateEdit::KeyDown(const char* bytes, int32 numBytes)
{
	if (IsEnabled() == false)
		return;
	SectionEdit::KeyDown(bytes, numBytes);

	// only accept valid input
	int32 number = atoi(bytes);
	if (number < 0 || bytes[0] < '0')
		return;

	int32 section = FocusIndex();
	if (section < 0 || section > 2)
		return;

	bigtime_t currentTime = system_time();
	if (currentTime - fLastKeyDownTime < 1000000) {
		int32 doubleDigit = number + fLastKeyDownInt * 10;
		if (_IsValidDoubleDigit(doubleDigit))
			number = doubleDigit;
		fLastKeyDownTime = 0;
	} else {
		fLastKeyDownTime = currentTime;
		fLastKeyDownInt = number;
	}

	// if year add 2000

	if (fFields[section] == B_DATE_ELEMENT_YEAR) {
		int32 oldCentury = int32(fHoldValue / 100) * 100;
		if (number < 10 && oldCentury == 1900)
			number += 70;
		number += oldCentury;
	}
	fHoldValue = number;

	// update display value
	_CheckRange();
	_UpdateFields();

	// send message to change time
	Invoke();
}


/** @brief Initialises the control by reading the current local date. */
void
DateEdit::InitView()
{
	// make sure we call the base class method, as it
	// will create the arrow bitmaps and the section list
	fDate = BDate::CurrentDate(B_LOCAL_TIME);
	_UpdateFields();
}


/** @brief Draws a single date section (day, month, or year).
 *  @param index   Zero-based section index.
 *  @param bounds  The rectangle allocated to this section.
 *  @param hasFocus True if this section has keyboard focus.
 */
void
DateEdit::DrawSection(uint32 index, BRect bounds, bool hasFocus)
{
	if (fFieldPositions == NULL || index * 2 + 1 >= (uint32)fFieldPosCount)
		return;

	if (hasFocus)
		SetLowColor(mix_color(ui_color(B_CONTROL_HIGHLIGHT_COLOR),
			ui_color(B_DOCUMENT_BACKGROUND_COLOR), 192));
	else
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	BString field;
	fText.CopyCharsInto(field, fFieldPositions[index * 2],
		fFieldPositions[index * 2 + 1] - fFieldPositions[index * 2]);

	BPoint point(bounds.LeftBottom());
	point.y -= bounds.Height() / 2.0 - 6.0;
	point.x += (bounds.Width() - StringWidth(field)) / 2;
	FillRect(bounds, B_SOLID_LOW);
	DrawString(field, point);
}


/** @brief Draws the separator character between two date sections.
 *  @param index   Zero-based separator index.
 *  @param bounds  The rectangle allocated to this separator.
 */
void
DateEdit::DrawSeparator(uint32 index, BRect bounds)
{
	if (index >= 2)
		return;

	if (fFieldPositions == NULL || index * 2 + 2 >= (uint32)fFieldPosCount)
		return;

	BString field;
	fText.CopyCharsInto(field, fFieldPositions[index * 2 + 1],
		fFieldPositions[index * 2 + 2] - fFieldPositions[index * 2 + 1]);

	BPoint point(bounds.LeftBottom());
	point.y -= bounds.Height() / 2.0 - 6.0;
	point.x += (bounds.Width() - StringWidth(field)) / 2;
	DrawString(field, point);
}


/** @brief Sets focus to the date section at the given index.
 *  @param index  Zero-based section index to focus.
 */
void
DateEdit::SectionFocus(uint32 index)
{
	fLastKeyDownTime = 0;
	fFocus = index;
	fHoldValue = _SectionValue(index);
	Draw(Bounds());
}


/** @brief Returns the minimum width for a single date section.
 *  @return Width of the string "00" in the plain font.
 */
float
DateEdit::MinSectionWidth()
{
	return be_plain_font->StringWidth("00");
}


/** @brief Returns the width allocated to each separator in pixels.
 *  @return Fixed separator width of 10 pixels.
 */
float
DateEdit::SeparatorWidth()
{
	return 10.0f;
}


/** @brief Sets the displayed date to the given year, month, and day.
 *  @param year   Four-digit year.
 *  @param month  Month number (1-12).
 *  @param day    Day of month (1-based).
 */
void
DateEdit::SetDate(int32 year, int32 month, int32 day)
{
	fDate.SetDate(year, month, day);

	if (LockLooper()) {
		_UpdateFields();
		UnlockLooper();
	}

	Invalidate(Bounds());
}


/** @brief Returns the current date value.
 *  @return A BDate representing the year, month, and day currently shown.
 */
BDate
DateEdit::GetDate()
{
	return fDate;
}


/** @brief Increments the focused date section by one unit. */
void
DateEdit::DoUpPress()
{
	if (fFocus == -1)
		SectionFocus(0);

	// update displayed value
	fHoldValue += 1;

	_CheckRange();
	_UpdateFields();

	// send message to change Date
	Invoke();
}


/** @brief Decrements the focused date section by one unit. */
void
DateEdit::DoDownPress()
{
	if (fFocus == -1)
		SectionFocus(0);

	// update display value
	fHoldValue -= 1;

	_CheckRange();
	_UpdateFields();

	// send message to change Date
	Invoke();
}


/** @brief Populates the outgoing message with the current year, month, and day.
 *  @param message  The message to fill in.
 */
void
DateEdit::PopulateMessage(BMessage* message)
{
	if (fFocus < 0 || fFocus >= fFieldCount)
		return;

	message->AddBool("time", false);
	message->AddInt32("year", fDate.Year());
	message->AddInt32("month", fDate.Month());
	message->AddInt32("day", fDate.Day());
}


/** @brief Reformats the display string and refreshes field-position metadata. */
void
DateEdit::_UpdateFields()
{
	time_t time = BDateTime(fDate, BTime()).Time_t();

	if (fFieldPositions != NULL) {
		free(fFieldPositions);
		fFieldPositions = NULL;
	}

	fDateFormat.Format(fText, fFieldPositions, fFieldPosCount, time,
		B_SHORT_DATE_FORMAT);

	if (fFields != NULL) {
		free(fFields);
		fFields = NULL;
	}
	fDateFormat.GetFields(fFields, fFieldCount, B_SHORT_DATE_FORMAT);
}


/** @brief Clamps fHoldValue to the valid range for the focused field and applies it.
 *
 *  For month changes, the day is clamped to the number of days in the new month.
 */
void
DateEdit::_CheckRange()
{
	if (fFocus < 0 || fFocus >= fFieldCount)
		return;

	int32 value = fHoldValue;
	switch (fFields[fFocus]) {
		case B_DATE_ELEMENT_DAY:
		{
			int32 days = fDate.DaysInMonth();
			if (value > days)
				value = 1;
			else if (value < 1)
				value = days;

			fDate.SetDate(fDate.Year(), fDate.Month(), value);
			break;
		}

		case B_DATE_ELEMENT_MONTH:
		{
			if (value > 12)
				value = 1;
			else if (value < 1)
				value = 12;

			int32 day = fDate.Day();
			fDate.SetDate(fDate.Year(), value, 1);

			// changing between months with differing amounts of days
			while (day > fDate.DaysInMonth())
				day--;
			fDate.SetDate(fDate.Year(), value, day);
			break;
		}

		case B_DATE_ELEMENT_YEAR:
			fDate.SetDate(value, fDate.Month(), fDate.Day());
			break;

		default:
			return;
	}

	fHoldValue = value;
	Invalidate(Bounds());
}


/** @brief Returns whether a two-digit value is within the focused field's range.
 *  @param value  The candidate two-digit integer.
 *  @return True if \a value is a valid entry for the focused date field.
 */
bool
DateEdit::_IsValidDoubleDigit(int32 value)
{
	if (fFocus < 0 || fFocus >= fFieldCount)
		return false;

	bool isInRange = false;
	switch (fFields[fFocus]) {
		case B_DATE_ELEMENT_DAY:
		{
			int32 days = fDate.DaysInMonth();
			if (value >= 1 && value <= days)
				isInRange = true;
			break;
		}

		case B_DATE_ELEMENT_MONTH:
		{
			if (value >= 1 && value <= 12)
				isInRange = true;
			break;
		}

		case B_DATE_ELEMENT_YEAR:
		{
			int32 year = int32(fHoldValue / 100) * 100 + value;
			if (year >= 2000)
				isInRange = true;
			break;
		}

		default:
			break;
	}

	return isInRange;
}


/** @brief Returns the current numeric value of a date section.
 *  @param index  Zero-based section index.
 *  @return The year, month, or day value, or 0 if the index is invalid.
 */
int32
DateEdit::_SectionValue(int32 index) const
{
	if (index < 0 || index >= fFieldCount)
		return 0;

	int32 value = 0;
	switch (fFields[index]) {
		case B_DATE_ELEMENT_YEAR:
			value = fDate.Year();
			break;

		case B_DATE_ELEMENT_MONTH:
			value = fDate.Month();
			break;

		case B_DATE_ELEMENT_DAY:
			value = fDate.Day();
			break;

		default:
			break;
	}

	return value;
}


/** @brief Returns the preferred height for the date editor.
 *  @return Ceiling of 1.4 × (ascent + descent) of the current font.
 */
float
DateEdit::PreferredHeight()
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	return ceilf((fontHeight.ascent + fontHeight.descent) * 1.4);
}


// #pragma mark -


/** @brief Constructs a SectionEdit base control.
 *  @param name      Internal view name.
 *  @param sections  Number of editable sections.
 *  @param message   The message sent on value changes.
 */
SectionEdit::SectionEdit(const char* name, uint32 sections, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW | B_NAVIGABLE),
	fFocus(-1),
	fSectionCount(sections),
	fHoldValue(0)
{
}


/** @brief Destructor. */
SectionEdit::~SectionEdit()
{
}


/** @brief Sets the document text color when attached to a window. */
void
SectionEdit::AttachedToWindow()
{
	BControl::AttachedToWindow();

	// Low colors are set in Draw() methods.
	SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
}


/** @brief Draws the border and all sections with their separators.
 *  @param updateRect  The dirty rectangle.
 */
void
SectionEdit::Draw(BRect updateRect)
{
	DrawBorder(updateRect);

	for (uint32 idx = 0; idx < fSectionCount; idx++) {
		DrawSection(idx, FrameForSection(idx),
			((uint32)fFocus == idx) && IsFocus());
		if (idx < fSectionCount - 1)
			DrawSeparator(idx, FrameForSeparator(idx));
	}
}


/** @brief Handles mouse clicks to focus the view or activate up/down arrows.
 *  @param where  The position of the click (view coordinates).
 */
void
SectionEdit::MouseDown(BPoint where)
{
	if (IsEnabled() == false)
		return;

	MakeFocus(true);

	if (fUpRect.Contains(where))
		DoUpPress();
	else if (fDownRect.Contains(where))
		DoDownPress();
	else if (fSectionCount > 0) {
		for (uint32 idx = 0; idx < fSectionCount; idx++) {
			if (FrameForSection(idx).Contains(where)) {
				SectionFocus(idx);
				return;
			}
		}
	}
}


/** @brief Returns the maximum size (unlimited width, fixed preferred height).
 *  @return A BSize with B_SIZE_UNLIMITED width and PreferredHeight() height.
 */
BSize
SectionEdit::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, PreferredHeight()));
}


/** @brief Returns the minimum size based on section and separator widths.
 *  @return A BSize with the minimum viable width and PreferredHeight().
 */
BSize
SectionEdit::MinSize()
{
	BSize minSize;
	minSize.height = PreferredHeight();
	minSize.width = (SeparatorWidth() + MinSectionWidth())
		* fSectionCount;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		minSize);
}


/** @brief Returns the preferred size, equal to the minimum size.
 *  @return The result of MinSize().
 */
BSize
SectionEdit::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		MinSize());
}


/** @brief Computes the bounding rectangle for a numbered section.
 *  @param index  Zero-based section index.
 *  @return The rectangle allocated to that section within the section area.
 */
BRect
SectionEdit::FrameForSection(uint32 index)
{
	BRect area = SectionArea();
	float sepWidth = SeparatorWidth();

	float width = (area.Width() -
		sepWidth * (fSectionCount - 1))
		/ fSectionCount;
	area.left += index * (width + sepWidth);
	area.right = area.left + width;

	return area;
}


/** @brief Computes the bounding rectangle for a numbered separator.
 *  @param index  Zero-based separator index.
 *  @return The rectangle allocated to that separator within the section area.
 */
BRect
SectionEdit::FrameForSeparator(uint32 index)
{
	BRect area = SectionArea();
	float sepWidth = SeparatorWidth();

	float width = (area.Width() -
		sepWidth * (fSectionCount - 1))
		/ fSectionCount;
	area.left += (index + 1) * width + index * sepWidth;
	area.right = area.left + sepWidth;

	return area;
}


/** @brief Focuses the control, ensuring a section is focused on first entry.
 *  @param focused  True to give focus; false to remove it.
 */
void
SectionEdit::MakeFocus(bool focused)
{
	if (focused == IsFocus())
		return;

	BControl::MakeFocus(focused);

	if (fFocus == -1)
		SectionFocus(0);
	else
		SectionFocus(fFocus);
}


/** @brief Handles arrow-key navigation between sections and up/down value changes.
 *  @param bytes     Pointer to the key bytes.
 *  @param numbytes  Number of bytes.
 */
void
SectionEdit::KeyDown(const char* bytes, int32 numbytes)
{
	if (IsEnabled() == false)
		return;
	if (fFocus == -1)
		SectionFocus(0);

	switch (bytes[0]) {
		case B_LEFT_ARROW:
			fFocus -= 1;
			if (fFocus < 0)
				fFocus = fSectionCount - 1;
			SectionFocus(fFocus);
			break;

		case B_RIGHT_ARROW:
			fFocus += 1;
			if ((uint32)fFocus >= fSectionCount)
				fFocus = 0;
			SectionFocus(fFocus);
			break;

		case B_UP_ARROW:
			DoUpPress();
			break;

		case B_DOWN_ARROW:
			DoDownPress();
			break;

		default:
			BControl::KeyDown(bytes, numbytes);
			break;
	}
	Draw(Bounds());
}


/** @brief Invokes the control, populating the message via PopulateMessage().
 *  @param message  The message to send, or NULL to use the stored message.
 *  @return Status code from BControl::Invoke().
 */
status_t
SectionEdit::Invoke(BMessage* message)
{
	if (message == NULL)
		message = Message();
	if (message == NULL)
		return BControl::Invoke(NULL);

	BMessage clone(*message);
	PopulateMessage(&clone);
	return BControl::Invoke(&clone);
}


/** @brief Returns the number of editable sections.
 *  @return Section count as supplied to the constructor.
 */
uint32
SectionEdit::CountSections() const
{
	return fSectionCount;
}


/** @brief Returns the index of the section that currently has focus.
 *  @return Zero-based focus index, or -1 if no section is focused.
 */
int32
SectionEdit::FocusIndex() const
{
	return fFocus;
}


/** @brief Returns the rectangle of the area available to sections (excluding arrows).
 *  @return The section area BRect, inset by 2 px on all sides.
 */
BRect
SectionEdit::SectionArea() const
{
	BRect sectionArea = Bounds().InsetByCopy(2, 2);
	sectionArea.right -= kArrowAreaWidth;
	return sectionArea;
}


/** @brief Draws the control border, background, and the up/down arrow indicators.
 *  @param updateRect  The dirty rectangle.
 */
void
SectionEdit::DrawBorder(const BRect& updateRect)
{
	BRect bounds(Bounds());
	bool showFocus = (IsFocus() && Window() && Window()->IsActive());

	be_control_look->DrawTextControlBorder(this, bounds, updateRect, ViewColor(),
		showFocus ? BControlLook::B_FOCUSED : 0);

	SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	FillRect(bounds, B_SOLID_LOW);

	// draw up/down control

	bounds.left = bounds.right - kArrowAreaWidth;
	bounds.right = Bounds().right - 2;
	fUpRect.Set(bounds.left + 3, bounds.top + 2, bounds.right,
		bounds.bottom / 2.0);
	fDownRect = fUpRect.OffsetByCopy(0, fUpRect.Height() + 2);

	BPoint middle(floorf(fUpRect.left + fUpRect.Width() / 2),
		fUpRect.top + 1);
	BPoint left(fUpRect.left + 3, fUpRect.bottom - 1);
	BPoint right(left.x + 2 * (middle.x - left.x), fUpRect.bottom - 1);

	SetPenSize(2);

	if (updateRect.Intersects(fUpRect)) {
		FillRect(fUpRect, B_SOLID_LOW);
		BeginLineArray(2);
			AddLine(left, middle, HighColor());
			AddLine(middle, right, HighColor());
		EndLineArray();
	}
	if (updateRect.Intersects(fDownRect)) {
		middle.y = fDownRect.bottom - 1;
		left.y = right.y = fDownRect.top + 1;

		FillRect(fDownRect, B_SOLID_LOW);
		BeginLineArray(2);
			AddLine(left, middle, HighColor());
			AddLine(middle, right, HighColor());
		EndLineArray();
	}

	SetPenSize(1);
}


}	// namespace BPrivate
