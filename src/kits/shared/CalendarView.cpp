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
 *   Copyright 2007-2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Julun <host.haiku@gmx.de>
 */

/** @file CalendarView.cpp
 *  @brief Implements BCalendarView, an interactive monthly calendar widget
 *         that supports selection, keyboard navigation, invocation messages,
 *         and optional day-name and week-number headers.
 */


#include "CalendarView.h"

#include <stdlib.h>

#include <DateFormat.h>
#include <LayoutUtils.h>
#include <Window.h>


namespace BPrivate {


/** @brief Returns the total pixel height of the font used by a view.
 *  @param view  The view whose font metrics are queried.
 *  @return Ceiling of ascent + descent + leading, or 0 if view is NULL.
 */
static float
FontHeight(const BView* view)
{
	if (!view)
		return 0.0;

	BFont font;
	view->GetFont(&font);
	font_height fheight;
	font.GetHeight(&fheight);
	return ceilf(fheight.ascent + fheight.descent + fheight.leading);
}


// #pragma mark -


/** @brief Constructs a BCalendarView with an explicit frame rectangle.
 *  @param frame       Initial position and size.
 *  @param name        Internal view name.
 *  @param resizeMask  Resize mask flags.
 *  @param flags       View flags.
 */
BCalendarView::BCalendarView(BRect frame, const char* name, uint32 resizeMask,
	uint32 flags)
	:
	BView(frame, name, resizeMask, flags),
	BInvoker(),
	fSelectionMessage(NULL),
	fDate(),
	fCurrentDate(BDate::CurrentDate(B_LOCAL_TIME)),
	fFocusChanged(false),
	fSelectionChanged(false),
	fCurrentDayChanged(false),
	fStartOfWeek((int32)B_WEEKDAY_MONDAY),
	fDayNameHeaderVisible(true),
	fWeekNumberHeaderVisible(true)
{
	_InitObject();
}


/** @brief Constructs a BCalendarView for use in a layout (no explicit frame).
 *  @param name   Internal view name.
 *  @param flags  View flags.
 */
BCalendarView::BCalendarView(const char* name, uint32 flags)
	:
	BView(name, flags),
	BInvoker(),
	fSelectionMessage(NULL),
	fDate(),
	fCurrentDate(BDate::CurrentDate(B_LOCAL_TIME)),
	fFocusChanged(false),
	fSelectionChanged(false),
	fCurrentDayChanged(false),
	fStartOfWeek((int32)B_WEEKDAY_MONDAY),
	fDayNameHeaderVisible(true),
	fWeekNumberHeaderVisible(true)
{
	_InitObject();
}


/** @brief Destructor — releases the selection message. */
BCalendarView::~BCalendarView()
{
	SetSelectionMessage(NULL);
}


/** @brief Unarchiving constructor — restores state from a BMessage archive.
 *  @param archive  The archive message produced by Archive().
 */
BCalendarView::BCalendarView(BMessage* archive)
	:
	BView(archive),
	BInvoker(),
	fSelectionMessage(NULL),
	fDate(archive),
	fCurrentDate(BDate::CurrentDate(B_LOCAL_TIME)),
	fFocusChanged(false),
	fSelectionChanged(false),
	fCurrentDayChanged(false),
	fStartOfWeek((int32)B_WEEKDAY_MONDAY),
	fDayNameHeaderVisible(true),
	fWeekNumberHeaderVisible(true)
{
	if (archive->HasMessage("_invokeMsg")) {
		BMessage* invokationMessage = new BMessage;
		archive->FindMessage("_invokeMsg", invokationMessage);
		SetInvocationMessage(invokationMessage);
	}

	if (archive->HasMessage("_selectMsg")) {
		BMessage* selectionMessage = new BMessage;
		archive->FindMessage("selectMsg", selectionMessage);
		SetSelectionMessage(selectionMessage);
	}

	if (archive->FindInt32("_weekStart", &fStartOfWeek) != B_OK)
		fStartOfWeek = (int32)B_WEEKDAY_MONDAY;

	if (archive->FindBool("_dayHeader", &fDayNameHeaderVisible) != B_OK)
		fDayNameHeaderVisible = true;

	if (archive->FindBool("_weekHeader", &fWeekNumberHeaderVisible) != B_OK)
		fWeekNumberHeaderVisible = true;

	_SetupDayNames();
	_SetupDayNumbers();
	_SetupWeekNumbers();
}


/** @brief Instantiates a BCalendarView from an archive message.
 *  @param archive  The archive to restore from.
 *  @return A new BCalendarView, or NULL if the archive is invalid.
 */
BArchivable*
BCalendarView::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BCalendarView"))
		return new BCalendarView(archive);

	return NULL;
}


/** @brief Archives the view state into a BMessage.
 *  @param archive  The destination archive message.
 *  @param deep     If true, child views are also archived.
 *  @return B_OK on success, or an error code.
 */
status_t
BCalendarView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);

	if (status == B_OK && InvocationMessage())
		status = archive->AddMessage("_invokeMsg", InvocationMessage());

	if (status == B_OK && SelectionMessage())
		status = archive->AddMessage("_selectMsg", SelectionMessage());

	if (status == B_OK)
		status = fDate.Archive(archive);

	if (status == B_OK)
		status = archive->AddInt32("_weekStart", fStartOfWeek);

	if (status == B_OK)
		status = archive->AddBool("_dayHeader", fDayNameHeaderVisible);

	if (status == B_OK)
		status = archive->AddBool("_weekHeader", fWeekNumberHeaderVisible);

	return status;
}


/** @brief Sets up the view color and default message target when attached. */
void
BCalendarView::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (!Messenger().IsValid())
		SetTarget(Window(), NULL);

	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
}


/** @brief Rebuilds day name strings and invalidates the calendar on resize.
 *  @param width   New view width.
 *  @param height  New view height.
 */
void
BCalendarView::FrameResized(float width, float height)
{
	_SetupDayNames();
	Invalidate(Bounds());
}


/** @brief Draws the calendar, dispatching to optimised partial-update paths.
 *
 *  When only the focus rectangle, selection, or current day has changed,
 *  only the affected cells are redrawn.  Otherwise the full calendar
 *  (days, day-name header, week-number header, and border) is repainted.
 *
 *  @param updateRect  The dirty rectangle that needs redrawing.
 */
void
BCalendarView::Draw(BRect updateRect)
{
	if (LockLooper()) {
		if (fFocusChanged) {
			_DrawFocusRect();
			UnlockLooper();
			return;
		}

		if (fSelectionChanged) {
			_UpdateSelection();
			UnlockLooper();
			return;
		}

		if (fCurrentDayChanged) {
			_UpdateCurrentDay();
			UnlockLooper();
			return;
		}

		_DrawDays();
		_DrawDayHeader();
		_DrawWeekHeader();

		rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
		SetHighColor(tint_color(background, B_DARKEN_3_TINT));
		StrokeRect(Bounds());

		UnlockLooper();
	}
}


/** @brief Draws a single day cell; may be overridden for custom rendering.
 *  @param owner      The view to draw into.
 *  @param frame      The cell rectangle.
 *  @param text       The day number string.
 *  @param isSelected True if this day is the selected date.
 *  @param isEnabled  True if this day belongs to the displayed month.
 *  @param focus      True if this cell has keyboard focus.
 *  @param highlight  True if this cell represents today.
 */
void
BCalendarView::DrawDay(BView* owner, BRect frame, const char* text,
	bool isSelected, bool isEnabled, bool focus, bool highlight)
{
	_DrawItem(owner, frame, text, isSelected, isEnabled, focus, highlight);
}


/** @brief Draws a day-name header cell; may be overridden.
 *  @param owner  The view to draw into.
 *  @param frame  The cell rectangle.
 *  @param text   The abbreviated day name string.
 */
void
BCalendarView::DrawDayName(BView* owner, BRect frame, const char* text)
{
	// we get the full rect, fake this as the internal function
	// shrinks the frame to work properly when drawing a day item
	_DrawItem(owner, frame.InsetByCopy(-1.0, -1.0), text, true);
}


/** @brief Draws a week-number header cell; may be overridden.
 *  @param owner  The view to draw into.
 *  @param frame  The cell rectangle.
 *  @param text   The week number string.
 */
void
BCalendarView::DrawWeekNumber(BView* owner, BRect frame, const char* text)
{
	// we get the full rect, fake this as the internal function
	// shrinks the frame to work properly when drawing a day item
	_DrawItem(owner, frame.InsetByCopy(-1.0, -1.0), text, true);
}


/** @brief Returns the 'what' field of the selection message, or 0 if none.
 *  @return The selection command constant.
 */
uint32
BCalendarView::SelectionCommand() const
{
	if (SelectionMessage())
		return SelectionMessage()->what;

	return 0;
}


/** @brief Returns the selection message.
 *  @return Pointer to the current selection BMessage, or NULL.
 */
BMessage*
BCalendarView::SelectionMessage() const
{
	return fSelectionMessage;
}


/** @brief Sets the selection message, replacing any previous one.
 *  @param message  The new selection message (adopted; may be NULL to clear).
 */
void
BCalendarView::SetSelectionMessage(BMessage* message)
{
	delete fSelectionMessage;
	fSelectionMessage = message;
}


/** @brief Returns the invocation command constant.
 *  @return The 'what' field of the invocation message.
 */
uint32
BCalendarView::InvocationCommand() const
{
	return BInvoker::Command();
}


/** @brief Returns the invocation message.
 *  @return Pointer to the current invocation BMessage, or NULL.
 */
BMessage*
BCalendarView::InvocationMessage() const
{
	return BInvoker::Message();
}


/** @brief Sets the invocation message.
 *  @param message  The new invocation message (adopted).
 */
void
BCalendarView::SetInvocationMessage(BMessage* message)
{
	BInvoker::SetMessage(message);
}


/** @brief Updates focus state and redraws the focus rectangle.
 *  @param state  True to give focus; false to remove it.
 */
void
BCalendarView::MakeFocus(bool state)
{
	if (IsFocus() == state)
		return;

	BView::MakeFocus(state);

	// TODO: solve this better
	fFocusChanged = true;
	Draw(_RectOfDay(fFocusedDay));
	fFocusChanged = false;
}


/** @brief Sends the invocation message augmented with the selected date.
 *
 *  Adds "year", "month", "day", "source", "when", and "be:sender" fields
 *  to a clone of the message before sending.  Also notifies watchers.
 *
 *  @param message  The message to send, or NULL to use the stored message.
 *  @return B_OK on success, B_BAD_VALUE if no message and no watchers.
 */
status_t
BCalendarView::Invoke(BMessage* message)
{
	bool notify = false;
	uint32 kind = InvokeKind(&notify);

	BMessage clone(kind);
	status_t status = B_BAD_VALUE;

	if (!message && !notify)
		message = Message();

	if (!message) {
		if (!IsWatched())
			return status;
	} else
		clone = *message;

	clone.AddPointer("source", this);
	clone.AddInt64("when", (int64)system_time());
	clone.AddMessenger("be:sender", BMessenger(this));

	clone.AddInt32("year", fDate.Year());
	clone.AddInt32("month", fDate.Month());
	clone.AddInt32("day", fDate.Day());

	if (message)
		status = BInvoker::Invoke(&clone);

	SendNotices(kind, &clone);

	return status;
}


/** @brief Handles mouse clicks to move focus and selection to the clicked day.
 *  @param where  The position of the click (view coordinates).
 */
void
BCalendarView::MouseDown(BPoint where)
{
	if (!IsFocus()) {
		MakeFocus();
		Sync();
		Window()->UpdateIfNeeded();
	}

	BRect frame = Bounds();
	if (fDayNameHeaderVisible)
		frame.top += frame.Height() / 7 - 1.0;

	if (fWeekNumberHeaderVisible)
		frame.left += frame.Width() / 8 - 1.0;

	if (!frame.Contains(where))
		return;

	// try to set to new day
	frame = _SetNewSelectedDay(where);

	// on success
	if (fSelectedDay != fNewSelectedDay) {
		// update focus
		fFocusChanged = true;
		fNewFocusedDay = fNewSelectedDay;
		Draw(_RectOfDay(fFocusedDay));
		fFocusChanged = false;

		// update selection
		fSelectionChanged = true;
		Draw(frame);
		Draw(_RectOfDay(fSelectedDay));
		fSelectionChanged = false;

		// notify that selection changed
		InvokeNotify(SelectionMessage(), B_CONTROL_MODIFIED);
	}

	int32 clicks;
	// on double click invoke
	BMessage* message = Looper()->CurrentMessage();
	if (message->FindInt32("clicks", &clicks) == B_OK && clicks > 1)
		Invoke();
}


/** @brief Handles keyboard navigation and date changes.
 *
 *  Arrow keys move the focus cell.  Page Up/Down change the month.
 *  Space/Return confirm the focused day as the selection.
 *
 *  @param bytes     Pointer to the key bytes.
 *  @param numBytes  Number of bytes in \a bytes.
 */
void
BCalendarView::KeyDown(const char* bytes, int32 numBytes)
{
	const int32 kRows = 6;
	const int32 kColumns = 7;

	int32 row = fFocusedDay.row;
	int32 column = fFocusedDay.column;

	switch (bytes[0]) {
		case B_LEFT_ARROW:
			column -= 1;
			if (column < 0) {
				column = kColumns - 1;
				row -= 1;
				if (row >= 0)
					fFocusChanged = true;
			} else
				fFocusChanged = true;
			break;

		case B_RIGHT_ARROW:
			column += 1;
			if (column == kColumns) {
				column = 0;
				row += 1;
				if (row < kRows)
					fFocusChanged = true;
			} else
				fFocusChanged = true;
			break;

		case B_UP_ARROW:
			row -= 1;
			if (row >= 0)
				fFocusChanged = true;
			break;

		case B_DOWN_ARROW:
			row += 1;
			if (row < kRows)
				fFocusChanged = true;
			break;

		case B_PAGE_UP:
		{
			BDate date(fDate);
			date.AddMonths(-1);
			SetDate(date);

			Invoke();
			break;
		}

		case B_PAGE_DOWN:
		{
			BDate date(fDate);
			date.AddMonths(1);
			SetDate(date);

			Invoke();
			break;
		}

		case B_RETURN:
		case B_SPACE:
		{
			fSelectionChanged = true;
			BPoint pt = _RectOfDay(fFocusedDay).LeftTop();
			Draw(_SetNewSelectedDay(pt + BPoint(4.0, 4.0)));
			Draw(_RectOfDay(fSelectedDay));
			fSelectionChanged = false;

			Invoke();
			break;
		}

		default:
			BView::KeyDown(bytes, numBytes);
			break;
	}

	if (fFocusChanged) {
		fNewFocusedDay.SetTo(row, column);
		Draw(_RectOfDay(fFocusedDay));
		Draw(_RectOfDay(fNewFocusedDay));
		fFocusChanged = false;
	}
}


/** @brief Pulse handler — checks whether the current calendar date has changed. */
void
BCalendarView::Pulse()
{
	_UpdateCurrentDate();
}


/** @brief Resizes the view to its preferred dimensions. */
void
BCalendarView::ResizeToPreferred()
{
	float width;
	float height;

	GetPreferredSize(&width, &height);
	BView::ResizeTo(width, height);
}


/** @brief Returns the preferred size via the internal helper.
 *  @param width   Output: preferred width.
 *  @param height  Output: preferred height.
 */
void
BCalendarView::GetPreferredSize(float* width, float* height)
{
	_GetPreferredSize(width, height);
}


/** @brief Returns the maximum size (unlimited in both dimensions).
 *  @return B_SIZE_UNLIMITED for width and height.
 */
BSize
BCalendarView::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
}


/** @brief Returns the minimum size, which equals the preferred size.
 *  @return A BSize computed by _GetPreferredSize().
 */
BSize
BCalendarView::MinSize()
{
	float width, height;
	_GetPreferredSize(&width, &height);
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), BSize(width, height));
}


/** @brief Returns the preferred size.
 *  @return The result of MinSize().
 */
BSize
BCalendarView::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), MinSize());
}


/** @brief Returns the currently selected day of month.
 *  @return Day number (1-based).
 */
int32
BCalendarView::Day() const
{
	return fDate.Day();
}


/** @brief Returns the currently selected year.
 *  @return Four-digit year.
 */
int32
BCalendarView::Year() const
{
	return fDate.Year();
}


/** @brief Returns the currently selected month.
 *  @return Month number (1 = January).
 */
int32
BCalendarView::Month() const
{
	return fDate.Month();
}


/** @brief Sets the selected day within the current month and year.
 *  @param day  The desired day number.
 *  @return True on success; false if the resulting date would be invalid.
 */
bool
BCalendarView::SetDay(int32 day)
{
	BDate date = Date();
	date.SetDay(day);
	if (!date.IsValid())
		return false;
	SetDate(date);
	return true;
}


/** @brief Sets the selected month, adjusting the day if the month is shorter.
 *  @param month  Month number (1-12).
 *  @return True on success; false if \a month is out of range.
 */
bool
BCalendarView::SetMonth(int32 month)
{
	if (month < 1 || month > 12)
		return false;
	BDate date = Date();
	int32 oldDay = date.Day();

	date.SetMonth(month);
	date.SetDay(1); // make sure the date is valid

	// We must make sure that the day in month fits inside the new month.
	if (oldDay > date.DaysInMonth())
		date.SetDay(date.DaysInMonth());
	else
		date.SetDay(oldDay);
	SetDate(date);
	return true;
}


/** @brief Sets the selected year, adjusting February 29 on non-leap years.
 *  @param year  The desired year.
 *  @return Always true (SetDate handles the adjustment).
 */
bool
BCalendarView::SetYear(int32 year)
{
	BDate date = Date();

	// This can fail when going from 29 feb. on a leap year to a non-leap year.
	if (date.Month() == 2 && date.Day() == 29 && !date.IsLeapYear(year))
		date.SetDay(28);

	// TODO we should also handle the "hole" at the switch between Julian and
	// Gregorian calendars, which will result in an invalid date.

	date.SetYear(year);
	SetDate(date);
	return true;
}


/** @brief Returns the currently selected date.
 *  @return A copy of the current BDate.
 */
BDate
BCalendarView::Date() const
{
	return fDate;
}


/** @brief Sets the selected date, rebuilding the calendar display as needed.
 *
 *  If the new date is in the same month, only the affected cells are
 *  redrawn.  Otherwise the full day grid is rebuilt and repainted.
 *
 *  @param date  The new date to display and select.
 *  @return True on success; false if \a date is invalid.
 */
bool
BCalendarView::SetDate(const BDate& date)
{
	if (!date.IsValid())
		return false;

	if (fDate == date)
		return true;

	if (fDate.Year() == date.Year() && fDate.Month() == date.Month()) {
		fDate = date;

		_SetToDay();
		// update focus
		fFocusChanged = true;
		Draw(_RectOfDay(fFocusedDay));
		fFocusChanged = false;

		// update selection
		fSelectionChanged = true;
		Draw(_RectOfDay(fSelectedDay));
		Draw(_RectOfDay(fNewSelectedDay));
		fSelectionChanged = false;
	} else {
		fDate = date;

		_SetupDayNumbers();
		_SetupWeekNumbers();

		BRect frame = Bounds();
		if (fDayNameHeaderVisible)
			frame.top += frame.Height() / 7 - 1.0;

		if (fWeekNumberHeaderVisible)
			frame.left += frame.Width() / 8 - 1.0;

		Draw(frame.InsetBySelf(4.0, 4.0));
	}

	return true;
}


/** @brief Sets the date from individual year, month, and day components.
 *  @param year   Four-digit year.
 *  @param month  Month number (1-12).
 *  @param day    Day of month (1-based).
 *  @return True on success; false if the resulting date is invalid.
 */
bool
BCalendarView::SetDate(int32 year, int32 month, int32 day)
{
	return SetDate(BDate(year, month, day));
}


/** @brief Returns the first day-of-week used as the left column of the grid.
 *  @return The current start-of-week BWeekday constant.
 */
BWeekday
BCalendarView::StartOfWeek() const
{
	return BWeekday(fStartOfWeek);
}


/** @brief Sets the first day of the week and rebuilds the calendar display.
 *  @param startOfWeek  The desired BWeekday constant.
 */
void
BCalendarView::SetStartOfWeek(BWeekday startOfWeek)
{
	if (fStartOfWeek == (int32)startOfWeek)
		return;

	fStartOfWeek = (int32)startOfWeek;

	_SetupDayNames();
	_SetupDayNumbers();
	_SetupWeekNumbers();

	Invalidate(Bounds().InsetBySelf(1.0, 1.0));
}


/** @brief Returns whether the row of abbreviated day names is shown.
 *  @return True if the day-name header row is visible.
 */
bool
BCalendarView::IsDayNameHeaderVisible() const
{
	return fDayNameHeaderVisible;
}


/** @brief Shows or hides the day-name header row.
 *  @param visible  True to show the header; false to hide it.
 */
void
BCalendarView::SetDayNameHeaderVisible(bool visible)
{
	if (fDayNameHeaderVisible == visible)
		return;

	fDayNameHeaderVisible = visible;
	Invalidate(Bounds().InsetBySelf(1.0, 1.0));
}


/** @brief Refreshes the day-name header using the current locale settings. */
void
BCalendarView::UpdateDayNameHeader()
{
	if (!fDayNameHeaderVisible)
		return;

	_SetupDayNames();
	Invalidate(Bounds().InsetBySelf(1.0, 1.0));
}


/** @brief Returns whether the column of ISO week numbers is shown.
 *  @return True if the week-number header column is visible.
 */
bool
BCalendarView::IsWeekNumberHeaderVisible() const
{
	return fWeekNumberHeaderVisible;
}


/** @brief Shows or hides the week-number header column.
 *  @param visible  True to show the column; false to hide it.
 */
void
BCalendarView::SetWeekNumberHeaderVisible(bool visible)
{
	if (fWeekNumberHeaderVisible == visible)
		return;

	fWeekNumberHeaderVisible = visible;
	Invalidate(Bounds().InsetBySelf(1.0, 1.0));
}


/** @brief Initialises the date, locale settings, and all cell data arrays. */
void
BCalendarView::_InitObject()
{
	fDate = BDate::CurrentDate(B_LOCAL_TIME);

	BDateFormat().GetStartOfWeek((BWeekday*)&fStartOfWeek);

	_SetupDayNames();
	_SetupDayNumbers();
	_SetupWeekNumbers();
}


/** @brief Locates the grid position of the current fDate and updates focus/selection. */
void
BCalendarView::_SetToDay()
{
	BDate date(fDate.Year(), fDate.Month(), 1);
	if (!date.IsValid())
		return;

	const int32 firstDayOffset = (7 + date.DayOfWeek() - fStartOfWeek) % 7;

	int32 day = 1 - firstDayOffset;
	for (int32 row = 0; row < 6; ++row) {
		for (int32 column = 0; column < 7; ++column) {
			if (day == fDate.Day()) {
				fNewFocusedDay.SetTo(row, column);
				fNewSelectedDay.SetTo(row, column);
				return;
			}
			day++;
		}
	}

	fNewFocusedDay.SetTo(0, 0);
	fNewSelectedDay.SetTo(0, 0);
}


/** @brief Locates the grid position of today's date and stores it in fNewCurrentDay.
 *
 *  If today is not in the currently displayed month, fNewCurrentDay is set
 *  to (-1, -1) to indicate that no cell should be highlighted.
 */
void
BCalendarView::_SetToCurrentDay()
{
	BDate date(fCurrentDate.Year(), fCurrentDate.Month(), 1);
	if (!date.IsValid())
		return;
	if (fDate.Year() != date.Year() || fDate.Month() != date.Month()) {
		fNewCurrentDay.SetTo(-1, -1);
		return;
	}
	const int32 firstDayOffset = (7 + date.DayOfWeek() - fStartOfWeek) % 7;

	int32 day = 1 - firstDayOffset;
	for (int32 row = 0; row < 6; ++row) {
		for (int32 column = 0; column < 7; ++column) {
			if (day == fCurrentDate.Day()) {
				fNewCurrentDay.SetTo(row, column);
				return;
			}
			day++;
		}
	}

	fNewCurrentDay.SetTo(-1, -1);
}


/** @brief Resolves the year and month for an arbitrary grid selection.
 *
 *  Days before the first of the month belong to the previous month; days
 *  after the last belong to the next month.
 *
 *  @param selection  The grid row/column pair to resolve.
 *  @param year       Output: year of the resolved date.
 *  @param month      Output: month of the resolved date.
 */
void
BCalendarView::_GetYearMonthForSelection(const Selection& selection,
	int32* year, int32* month) const
{
	BDate startOfMonth(fDate.Year(), fDate.Month(), 1);
	const int32 firstDayOffset
		= (7 + startOfMonth.DayOfWeek() - fStartOfWeek) % 7;
	const int32 daysInMonth = startOfMonth.DaysInMonth();

	BDate date(fDate);
	const int32 dayOffset = selection.row * 7 + selection.column;
	if (dayOffset < firstDayOffset)
		date.AddMonths(-1);
	else if (dayOffset >= firstDayOffset + daysInMonth)
		date.AddMonths(1);
	if (year != NULL)
		*year = date.Year();
	if (month != NULL)
		*month = date.Month();
}


/** @brief Calculates the preferred display dimensions.
 *  @param _width   Output: preferred width in pixels.
 *  @param _height  Output: preferred height in pixels.
 */
void
BCalendarView::_GetPreferredSize(float* _width, float* _height)
{
	BFont font;
	GetFont(&font);
	font_height fontHeight;
	font.GetHeight(&fontHeight);

	const float height = FontHeight(this) + 4.0;

	int32 rows = 7;
	if (!fDayNameHeaderVisible)
		rows = 6;

	// height = font height * rows + 8 px border
	*_height = height * rows + 8.0;

	float width = 0.0;
	for (int32 column = 0; column < 7; ++column) {
		float tmp = StringWidth(fDayNames[column].String()) + 2.0;
		width = tmp > width ? tmp : width;
	}

	int32 columns = 8;
	if (!fWeekNumberHeaderVisible)
		columns = 7;

	// width = max width day name * 8 column + 8 px border
	*_width = width * columns + 8.0;
}


/** @brief Selects the most compact day-name format that fits the current width. */
void
BCalendarView::_SetupDayNames()
{
	BDateFormatStyle style = B_LONG_DATE_FORMAT;
	float width, height;
	while (style !=  B_DATE_FORMAT_STYLE_COUNT) {
		_PopulateDayNames(style);
		GetPreferredSize(&width, &height);
		if (width < Bounds().Width())
			return;
		style = static_cast<BDateFormatStyle>(static_cast<int>(style) + 1);
	}
}


/** @brief Fills fDayNames[] with localised day name strings.
 *  @param style  The BDateFormatStyle to use (long, medium, short, etc.).
 */
void
BCalendarView::_PopulateDayNames(BDateFormatStyle style)
{
	for (int32 i = 0; i <= 6; ++i) {
		fDayNames[i] = "";
		BDateFormat().GetDayName(1 + (fStartOfWeek - 1 + i) % 7,
			fDayNames[i], style);
	}
}


/** @brief Rebuilds the fDayNumbers[][] grid and resets focus/selection to fDate. */
void
BCalendarView::_SetupDayNumbers()
{
	BDate startOfMonth(fDate.Year(), fDate.Month(), 1);
	if (!startOfMonth.IsValid())
		return;

	fFocusedDay.SetTo(0, 0);
	fSelectedDay.SetTo(0, 0);
	fNewFocusedDay.SetTo(0, 0);
	fCurrentDay.SetTo(-1, -1);

	const int32 daysInMonth = startOfMonth.DaysInMonth();
	const int32 firstDayOffset
		= (7 + startOfMonth.DayOfWeek() - fStartOfWeek) % 7;

	// calc the last day one month before
	BDate lastDayInMonthBefore(startOfMonth);
	lastDayInMonthBefore.AddDays(-1);
	const int32 lastDayBefore = lastDayInMonthBefore.DaysInMonth();

	int32 counter = 0;
	int32 firstDayAfter = 1;
	for (int32 row = 0; row < 6; ++row) {
		for (int32 column = 0; column < 7; ++column) {
			int32 day = 1 + counter - firstDayOffset;
			if (counter < firstDayOffset)
				day += lastDayBefore;
			else if (counter >= firstDayOffset + daysInMonth)
				day = firstDayAfter++;
			else if (day == fDate.Day()) {
				fFocusedDay.SetTo(row, column);
				fSelectedDay.SetTo(row, column);
				fNewFocusedDay.SetTo(row, column);
			}
			if (day == fCurrentDate.Day() && counter >= firstDayOffset
				&& counter < firstDayOffset + daysInMonth
				&& fDate.Month() == fCurrentDate.Month()
				&& fDate.Year() == fCurrentDate.Year())
				fCurrentDay.SetTo(row, column);

			counter++;
			fDayNumbers[row][column].Truncate(0);
			fDayNumbers[row][column] << day;
		}
	}
}


/** @brief Fills fWeekNumbers[] with ISO week number strings for each grid row. */
void
BCalendarView::_SetupWeekNumbers()
{
	BDate date(fDate.Year(), fDate.Month(), 1);
	if (!date.IsValid())
		return;

	for (int32 row = 0; row < 6; ++row) {
		fWeekNumbers[row].SetTo("");
		fWeekNumbers[row] << date.WeekNumber();
		date.AddDays(7);
	}
}


/** @brief Draws a single grid cell, respecting selection, enabled, focus, and highlight states.
 *  @param currRow     Row of the currently selected day.
 *  @param currColumn  Column of the currently selected day.
 *  @param row         Row of the cell being drawn.
 *  @param column      Column of the cell being drawn.
 *  @param counter     Linear cell index (1-based, row-major).
 *  @param frame       Bounding rectangle of the cell.
 *  @param text        The day number string to render.
 *  @param focus       True if this cell has keyboard focus.
 *  @param highlight   True if this cell represents today.
 */
void
BCalendarView::_DrawDay(int32 currRow, int32 currColumn, int32 row,
	int32 column, int32 counter, BRect frame, const char* text,
	bool focus, bool highlight)
{
	BDate startOfMonth(fDate.Year(), fDate.Month(), 1);
	const int32 firstDayOffset
		= (7 + startOfMonth.DayOfWeek() - fStartOfWeek) % 7;
	const int32 daysMonth = startOfMonth.DaysInMonth();

	bool enabled = true;
	bool selected = false;
	// check for the current date
	if (currRow == row  && currColumn == column) {
		selected = true;	// draw current date selected
		if (counter <= firstDayOffset || counter > firstDayOffset + daysMonth) {
			enabled = false;	// days of month before or after
			selected = false;	// not selected but able to get focus
		}
	} else {
		if (counter <= firstDayOffset || counter > firstDayOffset + daysMonth)
			enabled = false;	// days of month before or after
	}

	DrawDay(this, frame, text, selected, enabled, focus, highlight);
}


/** @brief Iterates over the 6x7 grid and draws every day cell. */
void
BCalendarView::_DrawDays()
{
	BRect frame = _FirstCalendarItemFrame();

	const int32 currRow = fSelectedDay.row;
	const int32 currColumn = fSelectedDay.column;

	const bool isFocus = IsFocus();
	const int32 focusRow = fFocusedDay.row;
	const int32 focusColumn = fFocusedDay.column;

	const int32 highlightRow = fCurrentDay.row;
	const int32 highlightColumn = fCurrentDay.column;

	int32 counter = 0;
	for (int32 row = 0; row < 6; ++row) {
		BRect tmp = frame;
		for (int32 column = 0; column < 7; ++column) {
			counter++;
			const char* day = fDayNumbers[row][column].String();
			bool focus = isFocus && focusRow == row && focusColumn == column;
			bool highlight = highlightRow == row && highlightColumn == column;
			_DrawDay(currRow, currColumn, row, column, counter, tmp, day,
				focus, highlight);

			tmp.OffsetBy(tmp.Width(), 0.0);
		}
		frame.OffsetBy(0.0, frame.Height());
	}
}


/** @brief Redraws only the old and new focus cells after a focus change. */
void
BCalendarView::_DrawFocusRect()
{
	BRect frame = _FirstCalendarItemFrame();

	const int32 currRow = fSelectedDay.row;
	const int32 currColumn = fSelectedDay.column;

	const int32 focusRow = fFocusedDay.row;
	const int32 focusColumn = fFocusedDay.column;

	const int32 highlightRow = fCurrentDay.row;
	const int32 highlightColumn = fCurrentDay.column;

	int32 counter = 0;
	for (int32 row = 0; row < 6; ++row) {
		BRect tmp = frame;
		for (int32 column = 0; column < 7; ++column) {
			counter++;
			if (fNewFocusedDay.row == row && fNewFocusedDay.column == column) {
				fFocusedDay.SetTo(row, column);

				bool focus = IsFocus() && true;
				bool highlight = highlightRow == row && highlightColumn == column;
				const char* day = fDayNumbers[row][column].String();
				_DrawDay(currRow, currColumn, row, column, counter, tmp, day,
					focus, highlight);
			} else if (focusRow == row && focusColumn == column) {
				const char* day = fDayNumbers[row][column].String();
				bool highlight = highlightRow == row && highlightColumn == column;
				_DrawDay(currRow, currColumn, row, column, counter, tmp, day,
					false, highlight);
			}
			tmp.OffsetBy(tmp.Width(), 0.0);
		}
		frame.OffsetBy(0.0, frame.Height());
	}
}


/** @brief Draws the row of abbreviated day-name headers across the top. */
void
BCalendarView::_DrawDayHeader()
{
	if (!fDayNameHeaderVisible)
		return;

	int32 offset = 1;
	int32 columns = 8;
	if (!fWeekNumberHeaderVisible) {
		offset = 0;
		columns = 7;
	}

	BRect frame = Bounds();
	frame.right = frame.Width() / columns - 1.0;
	frame.bottom = frame.Height() / 7.0 - 2.0;
	frame.OffsetBy(4.0, 4.0);

	for (int32 i = 0; i < columns; ++i) {
		if (i == 0 && fWeekNumberHeaderVisible) {
			DrawDayName(this, frame, "");
			frame.OffsetBy(frame.Width(), 0.0);
			continue;
		}
		DrawDayName(this, frame, fDayNames[i - offset].String());
		frame.OffsetBy(frame.Width(), 0.0);
	}
}


/** @brief Draws the column of ISO week numbers along the left edge. */
void
BCalendarView::_DrawWeekHeader()
{
	if (!fWeekNumberHeaderVisible)
		return;

	int32 rows = 7;
	if (!fDayNameHeaderVisible)
		rows = 6;

	BRect frame = Bounds();
	frame.right = frame.Width() / 8.0 - 2.0;
	frame.bottom = frame.Height() / rows - 1.0;

	float offsetY = 4.0;
	if (fDayNameHeaderVisible)
		offsetY += frame.Height();

	frame.OffsetBy(4.0, offsetY);

	for (int32 row = 0; row < 6; ++row) {
		DrawWeekNumber(this, frame, fWeekNumbers[row].String());
		frame.OffsetBy(0.0, frame.Height());
	}
}


/** @brief Draws a single calendar cell with background, focus ring, and text.
 *  @param owner       The view to draw into.
 *  @param frame       The cell rectangle.
 *  @param text        The string to render centered in the cell.
 *  @param isSelected  True if this cell is the selected date.
 *  @param isEnabled   True if this day is in the current month.
 *  @param focus       True if this cell has keyboard focus.
 *  @param isHighlight True if this cell represents today.
 */
void
BCalendarView::_DrawItem(BView* owner, BRect frame, const char* text,
	bool isSelected, bool isEnabled, bool focus, bool isHighlight)
{
	rgb_color lColor = LowColor();
	rgb_color highColor = HighColor();

	rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
	rgb_color bgColor = ui_color(B_LIST_BACKGROUND_COLOR);
	float tintDisabled = B_LIGHTEN_2_TINT;
	float tintHighlight = B_LIGHTEN_1_TINT;

	if (textColor.red + textColor.green + textColor.blue > 125 * 3)
		tintDisabled  = B_DARKEN_2_TINT;

	if (bgColor.red + bgColor.green + bgColor.blue > 125 * 3)
		tintHighlight = B_DARKEN_1_TINT;

	if (isSelected) {
		SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
		textColor = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	} else if (isHighlight)
		SetHighColor(tint_color(bgColor, tintHighlight));
	else
		SetHighColor(bgColor);

	SetLowColor(HighColor());

	FillRect(frame.InsetByCopy(1.0, 1.0));

	if (focus) {
		rgb_color focusColor = keyboard_navigation_color();
		SetHighColor(focusColor);
		StrokeRect(frame.InsetByCopy(1.0, 1.0));

		if (!isSelected)
			textColor = focusColor;
	}

	SetHighColor(textColor);
	if (!isEnabled)
		SetHighColor(tint_color(textColor, tintDisabled));

	float offsetH = frame.Width() / 2.0;
	float offsetV = frame.Height() / 2.0 + FontHeight(owner) / 4.0;

	BFont font(be_plain_font);
	if (isHighlight)
		font.SetFace(B_BOLD_FACE);
	else
		font.SetFace(B_REGULAR_FACE);
	SetFont(&font);

	DrawString(text, BPoint(frame.right - offsetH - StringWidth(text) / 2.0,
			frame.top + offsetV));

	SetLowColor(lColor);
	SetHighColor(highColor);
}


/** @brief Redraws only the old and new selection cells after a selection change. */
void
BCalendarView::_UpdateSelection()
{
	BRect frame = _FirstCalendarItemFrame();

	const int32 currRow = fSelectedDay.row;
	const int32 currColumn = fSelectedDay.column;

	const int32 focusRow = fFocusedDay.row;
	const int32 focusColumn = fFocusedDay.column;

	const int32 highlightRow = fCurrentDay.row;
	const int32 highlightColumn = fCurrentDay.column;

	int32 counter = 0;
	for (int32 row = 0; row < 6; ++row) {
		BRect tmp = frame;
		for (int32 column = 0; column < 7; ++column) {
			counter++;
			if (fNewSelectedDay.row == row
				&& fNewSelectedDay.column == column) {
				fSelectedDay.SetTo(row, column);

				const char* day = fDayNumbers[row][column].String();
				bool focus = IsFocus() && focusRow == row
					&& focusColumn == column;
				bool highlight = highlightRow == row && highlightColumn == column;
				_DrawDay(row, column, row, column, counter, tmp, day, focus, highlight);
			} else if (currRow == row && currColumn == column) {
				const char* day = fDayNumbers[row][column].String();
				bool focus = IsFocus() && focusRow == row
					&& focusColumn == column;
				bool highlight = highlightRow == row && highlightColumn == column;
				_DrawDay(currRow, currColumn, -1, -1, counter, tmp, day, focus, highlight);
			}
			tmp.OffsetBy(tmp.Width(), 0.0);
		}
		frame.OffsetBy(0.0, frame.Height());
	}
}


/** @brief Redraws only the old and new "today" highlight cells after the date changes. */
void
BCalendarView::_UpdateCurrentDay()
{
	BRect frame = _FirstCalendarItemFrame();

	const int32 selectRow = fSelectedDay.row;
	const int32 selectColumn = fSelectedDay.column;

	const int32 focusRow = fFocusedDay.row;
	const int32 focusColumn = fFocusedDay.column;

	const int32 currRow = fCurrentDay.row;
	const int32 currColumn = fCurrentDay.column;

	int32 counter = 0;
	for (int32 row = 0; row < 6; ++row) {
		BRect tmp = frame;
		for (int32 column = 0; column < 7; ++column) {
			counter++;
			if (fNewCurrentDay.row == row
				&& fNewCurrentDay.column == column) {
				fCurrentDay.SetTo(row, column);

				const char* day = fDayNumbers[row][column].String();
				bool focus = IsFocus() && focusRow == row
					&& focusColumn == column;
				bool isSelected = selectRow == row && selectColumn == column;
				if (isSelected)
					_DrawDay(row, column, row, column, counter, tmp, day, focus, true);
				else
					_DrawDay(row, column, -1, -1, counter, tmp, day, focus, true);

			} else if (currRow == row && currColumn == column) {
				const char* day = fDayNumbers[row][column].String();
				bool focus = IsFocus() && focusRow == row
					&& focusColumn == column;
				bool isSelected = selectRow == row && selectColumn == column;
				if(isSelected)
					_DrawDay(currRow, currColumn, row, column, counter, tmp, day, focus, false);
				else
					_DrawDay(currRow, currColumn, -1, -1, counter, tmp, day, focus, false);
			}
			tmp.OffsetBy(tmp.Width(), 0.0);
		}
		frame.OffsetBy(0.0, frame.Height());
	}
}


/** @brief Checks whether today's date has changed and triggers a redraw if so. */
void
BCalendarView::_UpdateCurrentDate()
{
	BDate date = BDate::CurrentDate(B_LOCAL_TIME);

	if (!date.IsValid())
		return;
	if (date == fCurrentDate)
		return;

	fCurrentDate = date;

	_SetToCurrentDay();
	fCurrentDayChanged = true;
	Draw(_RectOfDay(fCurrentDay));
	Draw(_RectOfDay(fNewCurrentDay));
	fCurrentDayChanged = false;

	return;
}


/** @brief Computes the bounding rectangle of the first (top-left) day cell.
 *  @return The BRect of the first calendar day cell.
 */
BRect
BCalendarView::_FirstCalendarItemFrame() const
{
	int32 rows = 7;
	int32 columns = 8;

	if (!fDayNameHeaderVisible)
		rows = 6;

	if (!fWeekNumberHeaderVisible)
		columns = 7;

	BRect frame = Bounds();
	frame.right = frame.Width() / columns - 1.0;
	frame.bottom = frame.Height() / rows - 1.0;

	float offsetY = 4.0;
	if (fDayNameHeaderVisible)
		offsetY += frame.Height();

	float offsetX = 4.0;
	if (fWeekNumberHeaderVisible)
		offsetX += frame.Width();

	return frame.OffsetBySelf(offsetX, offsetY);
}


/** @brief Finds the grid cell containing \a where and updates fNewSelectedDay.
 *
 *  If the clicked cell is in the current month, fDate is also updated to the
 *  clicked day.
 *
 *  @param where  The point to test (view coordinates).
 *  @return The bounding rectangle of the matched cell, or the last iterated
 *          frame if no match was found.
 */
BRect
BCalendarView::_SetNewSelectedDay(const BPoint& where)
{
	BRect frame = _FirstCalendarItemFrame();

	int32 counter = 0;
	for (int32 row = 0; row < 6; ++row) {
		BRect tmp = frame;
		for (int32 column = 0; column < 7; ++column) {
			counter++;
			if (tmp.Contains(where)) {
				fNewSelectedDay.SetTo(row, column);
				int32 year;
				int32 month;
				_GetYearMonthForSelection(fNewSelectedDay, &year, &month);
				if (month == fDate.Month()) {
					// only change date if a day in the current month has been
					// selected
					int32 day = atoi(fDayNumbers[row][column].String());
					fDate.SetDate(year, month, day);
				}
				return tmp;
			}
			tmp.OffsetBy(tmp.Width(), 0.0);
		}
		frame.OffsetBy(0.0, frame.Height());
	}

	return frame;
}


/** @brief Returns the bounding rectangle of the cell at \a selection.
 *  @param selection  The grid row/column pair to look up.
 *  @return The cell's bounding rectangle, or the last iterated frame if not found.
 */
BRect
BCalendarView::_RectOfDay(const Selection& selection) const
{
	BRect frame = _FirstCalendarItemFrame();

	int32 counter = 0;
	for (int32 row = 0; row < 6; ++row) {
		BRect tmp = frame;
		for (int32 column = 0; column < 7; ++column) {
			counter++;
			if (selection.row == row && selection.column == column)
				return tmp;
			tmp.OffsetBy(tmp.Width(), 0.0);
		}
		frame.OffsetBy(0.0, frame.Height());
	}

	return frame;
}


}	// namespace BPrivate
