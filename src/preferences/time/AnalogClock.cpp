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
 *   Copyright 2004-2024 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Mike Berg <mike@berg-net.us>
 *       Julun <host.haiku@gmx.de>
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Clemens <mail@Clemens-Zeidler.de>
 *       Hamish Morrison <hamish@lavabit.com>
 *       John Scipione <jscipione@gmail.com>
 *       Niklas Poslovski <ni.pos@yandex.com>
 */


/**
 * @file AnalogClock.cpp
 * @brief Implementation of the analog clock face used by the Time panel.
 *
 * Draws an antialiased dial with hour, minute, and optional second hands,
 * and supports drag-to-set interaction on the hour and minute hands. Hand
 * hit-testing is angle-based and tolerant of a small phi delta.
 */


#include "AnalogClock.h"

#include <math.h>
#include <stdio.h>

#include <LayoutUtils.h>
#include <Message.h>
#include <Window.h>

#include "TimeMessages.h"


/** @brief Maximum angular delta in radians within which a hand is "hit". */
#define DRAG_DELTA_PHI 0.2


/**
 * @brief Constructs the analog clock view.
 *
 * Initializes all coordinates to zero (DoLayout() recomputes them) and
 * marks the dial dirty so the first Draw() repaints fully.
 *
 * @param name           View name.
 * @param drawSecondHand When true, render a red second hand.
 * @param interactive    When true, allow drag-to-set on the hour/minute
 *                       hands.
 */
TAnalogClock::TAnalogClock(const char* name, bool drawSecondHand,
	bool interactive)
	:
	BView(name, B_WILL_DRAW | B_DRAW_ON_CHILDREN | B_FRAME_EVENTS),
	fHours(0),
	fMinutes(0),
	fSeconds(0),
	fDirty(true),
	fCenterX(0.0),
	fCenterY(0.0),
	fRadius(0.0),
	fHourDragging(false),
	fMinuteDragging(false),
	fDrawSecondHand(drawSecondHand),
	fInteractive(interactive),
	fTimeChangeIsOngoing(false)
{
	SetFlags(Flags() | B_SUBPIXEL_PRECISE);
}


/**
 * @brief Destructor; nothing to release.
 */
TAnalogClock::~TAnalogClock()
{
}


/**
 * @brief BView Draw() override; redraws the clock when dirty.
 *
 * @param updateRect Update rectangle (unused; the entire dial is redrawn).
 */
void
TAnalogClock::Draw(BRect /*updateRect*/)
{
	if (fDirty)
		DrawClock();
}


/**
 * @brief Receives clock-tick observer notices to keep the hands current.
 *
 * Looks for B_OBSERVER_NOTICE_CHANGE messages whose change code is
 * H_TM_CHANGED, extracts hour/minute/second, and forwards them to
 * SetTime(). All other messages fall through to BView.
 *
 * @param message Incoming message.
 */
void
TAnalogClock::MessageReceived(BMessage* message)
{
	int32 change;
	switch (message->what) {
		case B_OBSERVER_NOTICE_CHANGE:
			message->FindInt32(B_OBSERVE_WHAT_CHANGE, &change);
			switch (change) {
				case H_TM_CHANGED:
				{
					int32 hour = 0;
					int32 minute = 0;
					int32 second = 0;
					if (message->FindInt32("hour", &hour) == B_OK
					 && message->FindInt32("minute", &minute) == B_OK
					 && message->FindInt32("second", &second) == B_OK)
						SetTime(hour, minute, second);
					break;
				}
				default:
					BView::MessageReceived(message);
					break;
			}
		break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Begins dragging the hour or minute hand if the click hits one.
 *
 * In interactive mode, captures pointer events with B_LOCK_WINDOW_FOCUS so
 * the drag is not interrupted by other widgets. In non-interactive mode,
 * the click falls through to BView.
 *
 * @param point Click location in view coordinates.
 */
void
TAnalogClock::MouseDown(BPoint point)
{
	if (!fInteractive) {
		BView::MouseDown(point);
		return;
	}

	if (InMinuteHand(point)) {
		fMinuteDragging = true;
		fDirty = true;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		Invalidate();
		return;
	}

	if (InHourHand(point)) {
		fHourDragging = true;
		fDirty = true;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		Invalidate();
		return;
	}
}


/**
 * @brief Finishes a hand drag and posts an H_USER_CHANGE to the window.
 *
 * @param point Mouse-up location (unused, captured by drag state).
 */
void
TAnalogClock::MouseUp(BPoint point)
{
	if (!fInteractive) {
		BView::MouseUp(point);
		return;
	}

	if (fHourDragging || fMinuteDragging) {
		int32 hour, minute, second;
		GetTime(&hour, &minute, &second);
		BMessage message(H_USER_CHANGE);
		message.AddBool("time", true);
		message.AddInt32("hour", hour);
		message.AddInt32("minute", minute);
		Window()->PostMessage(&message);
		fTimeChangeIsOngoing = true;
	}
	fHourDragging = false;
	fDirty = true;
	fMinuteDragging = false;
	fDirty = true;
}


/**
 * @brief Updates the dragged hand to follow the cursor.
 *
 * @param point   Cursor position.
 * @param transit Cursor transit code (unused).
 * @param message Optional dragged message (unused).
 */
void
TAnalogClock::MouseMoved(BPoint point, uint32 transit, const BMessage* message)
{
	if (!fInteractive) {
		BView::MouseMoved(point, transit, message);
		return;
	}

	if (fMinuteDragging)
		SetMinuteHand(point);
	if (fHourDragging)
		SetHourHand(point);

	Invalidate();
}


/**
 * @brief Recomputes the dial geometry whenever the view is laid out.
 *
 * Centers the clock in its bounds and sets the dial radius to half the
 * shorter axis less a small inset so antialiased strokes stay inside the
 * frame.
 */
void
TAnalogClock::DoLayout()
{
	BRect bounds = Bounds();

	// + 0.5 is for the offset to pixel centers
	// (important when drawing with B_SUBPIXEL_PRECISE)
	fCenterX = floorf((bounds.left + bounds.right) / 2 + 0.5) + 0.5;
	fCenterY = floorf((bounds.top + bounds.bottom) / 2 + 0.5) + 0.5;
	fRadius = floorf((MIN(bounds.Width(), bounds.Height()) / 2.0)) - 5.5;
}


/**
 * @brief Returns an unbounded maximum size, composed with the explicit max.
 */
BSize
TAnalogClock::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
}


/**
 * @brief Returns the minimum readable size for the clock face.
 */
BSize
TAnalogClock::MinSize()
{
	return BSize(64.f, 64.f);
}


/**
 * @brief Returns the preferred size, composed with the explicit preferred.
 */
BSize
TAnalogClock::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
}


/**
 * @brief Updates the displayed time, suppressing changes while dragging.
 *
 * No-op when a hand drag is in progress, when an explicit time change is
 * pending, or when the supplied values match the cached values.
 *
 * @param hour   Hour in [0, 23].
 * @param minute Minute in [0, 59].
 * @param second Second in [0, 59].
 */
void
TAnalogClock::SetTime(int32 hour, int32 minute, int32 second)
{
	// don't set the time if the hands are in a drag action
	if (fHourDragging || fMinuteDragging || fTimeChangeIsOngoing)
		return;

	if (fHours == hour && fMinutes == minute && fSeconds == second)
		return;

	fHours = hour;
	fMinutes = minute;
	fSeconds = second;

	fDirty = true;

	BWindow* window = Window();
	if (window && window->Lock()) {
		Invalidate();
		Window()->Unlock();
	}
}


/**
 * @brief Returns true while a user-driven time change is still pending.
 */
bool
TAnalogClock::IsChangingTime()
{
	return fTimeChangeIsOngoing;
}


/**
 * @brief Clears the "change in progress" flag set by MouseUp().
 */
void
TAnalogClock::ChangeTimeFinished()
{
	fTimeChangeIsOngoing = false;
}


/**
 * @brief Returns the currently displayed time.
 *
 * @param hour   Output: hour in [0, 23].
 * @param minute Output: minute in [0, 59].
 * @param second Output: second in [0, 59].
 */
void
TAnalogClock::GetTime(int32* hour, int32* minute, int32* second)
{
	*hour = fHours;
	*minute = fMinutes;
	*second = fSeconds;
}


/**
 * @brief Renders the dial, ticks, and hands directly to the view.
 *
 * Locks the looper, paints the background, draws three concentric ring
 * outlines for the bezel highlight/shadow, then the minute and hour
 * tick marks, and finally a shadow pass and a foreground pass of the
 * hour/minute/second hands.
 */
void
TAnalogClock::DrawClock()
{
	if (!LockLooper())
		return;

	BRect bounds = Bounds();

	// clear background
	SetHighColor(ViewColor());
	FillRect(bounds);

	bounds.Set(fCenterX - fRadius, fCenterY - fRadius, fCenterX + fRadius, fCenterY + fRadius);

	SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
	SetPenSize(2.0);

	bool isLight = LowColor().IsLight();

	if (isLight)
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	else
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, 0.853);
	StrokeEllipse(bounds.OffsetByCopy(-1, -1));

	if (isLight)
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, B_LIGHTEN_2_TINT);
	else
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, 1.615);
	StrokeEllipse(bounds);

	if (isLight)
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, B_DARKEN_3_TINT);
	else
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, 0.593);
	StrokeEllipse(bounds.OffsetByCopy(1, 1));

	FillEllipse(bounds, B_SOLID_LOW);

	if (isLight)
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, B_DARKEN_2_TINT);
	else
		SetHighUIColor(B_DOCUMENT_BACKGROUND_COLOR, 0.705);

	// minutes
	SetPenSize(1.0);
	SetLineMode(B_BUTT_CAP, B_MITER_JOIN);
	for (int32 minute = 1; minute < 60; minute++) {
		if (minute % 5 == 0)
			continue;
		float x1 = fCenterX + sinf(minute * M_PI / 30.0) * fRadius;
		float y1 = fCenterY + cosf(minute * M_PI / 30.0) * fRadius;
		float x2 = fCenterX + sinf(minute * M_PI / 30.0) * (fRadius * 0.95);
		float y2 = fCenterY + cosf(minute * M_PI / 30.0) * (fRadius * 0.95);
		StrokeLine(BPoint(x1, y1), BPoint(x2, y2));
	}

	if (isLight)
		SetHighUIColor(B_DOCUMENT_TEXT_COLOR, B_DARKEN_1_TINT);
	else
		SetHighUIColor(B_DOCUMENT_TEXT_COLOR, 0.853);

	rgb_color handsColor = HighColor();

	// hours
	SetPenSize(2.0);
	SetLineMode(B_ROUND_CAP, B_MITER_JOIN);
	for (int32 hour = 0; hour < 12; hour++) {
		float x1 = fCenterX + sinf(hour * M_PI / 6.0) * fRadius;
		float y1 = fCenterY + cosf(hour * M_PI / 6.0) * fRadius;
		float x2 = fCenterX + sinf(hour * M_PI / 6.0) * (fRadius * 0.9);
		float y2 = fCenterY + cosf(hour * M_PI / 6.0) * (fRadius * 0.9);
		StrokeLine(BPoint(x1, y1), BPoint(x2, y2));
	}

	rgb_color hourColor;
	if (fHourDragging)
		hourColor = ui_color(B_NAVIGATION_BASE_COLOR);
	else
		hourColor = handsColor;

	rgb_color minuteColor;
	if (fMinuteDragging)
		minuteColor = ui_color(B_NAVIGATION_BASE_COLOR);
	else
		minuteColor = handsColor;

	rgb_color secondsColor = make_color(255, 0, 0, 255);
	rgb_color shadowColor = tint_color(LowColor(), (B_DARKEN_1_TINT + B_DARKEN_2_TINT) / 2);

	_DrawHands(fCenterX + 1.5, fCenterY + 1.5, fRadius, shadowColor, shadowColor, shadowColor,
		shadowColor);
	_DrawHands(fCenterX, fCenterY, fRadius, hourColor, minuteColor, secondsColor, handsColor);

	Sync();

	UnlockLooper();
}


/**
 * @brief Returns true when @a point lies within the hour hand's hit zone.
 *
 * @param point Click location in view coordinates.
 */
bool
TAnalogClock::InHourHand(BPoint point)
{
	int32 ticks = fHours;
	if (ticks > 12)
		ticks -= 12;
	ticks *= 5;
	ticks += int32(5. * fMinutes / 60.0);
	if (ticks > 60)
		ticks -= 60;
	return _InHand(point, ticks, fRadius * 0.7);
}


/**
 * @brief Returns true when @a point lies within the minute hand's hit zone.
 *
 * @param point Click location in view coordinates.
 */
bool
TAnalogClock::InMinuteHand(BPoint point)
{
	return _InHand(point, fMinutes, fRadius * 0.9);
}


/**
 * @brief Snaps the hour hand to the angle of the supplied point.
 *
 * Preserves the AM/PM half (above or below 12) so a small drag does not
 * silently jump twelve hours.
 *
 * @param point Cursor position in view coordinates.
 */
void
TAnalogClock::SetHourHand(BPoint point)
{
	point.x -= fCenterX;
	point.y -= fCenterY;

	float pointPhi = _GetPhi(point);
	float hoursExact = 6.0 * pointPhi / M_PI;
	if (fHours >= 12)
		fHours = 12;
	else
		fHours = 0;
	fHours += int32(hoursExact);

	SetTime(fHours, fMinutes, fSeconds);
}


/**
 * @brief Snaps the minute hand to the angle of the supplied point.
 *
 * @param point Cursor position in view coordinates.
 */
void
TAnalogClock::SetMinuteHand(BPoint point)
{
	point.x -= fCenterX;
	point.y -= fCenterY;

	float pointPhi = _GetPhi(point);
	float minutesExact = 30.0 * pointPhi / M_PI;
	fMinutes = int32(ceilf(minutesExact));

	SetTime(fHours, fMinutes, fSeconds);
}


/**
 * @brief Returns the clock-face angle (radians) of a center-relative point.
 *
 * Angles run clockwise from 12 o'clock so that they map directly onto the
 * minute scale used by SetMinuteHand() and the hour scale used by
 * SetHourHand(). The four cardinal directions are special-cased.
 *
 * @param point Center-relative point.
 * @return Angle in radians in the range [0, 2*pi).
 */
float
TAnalogClock::_GetPhi(BPoint point)
{
	if (point.x == 0 && point.y < 0)
		return 2 * M_PI;
	if (point.x == 0 && point.y > 0)
		return M_PI;
	if (point.y == 0 && point.x < 0)
		return M_PI * 3 / 2;
	if (point.y == 0 && point.x > 0)
		return M_PI / 2;

	float pointPhi = atanf(-1. * point.y / point.x);
	if (point.y < 0. && point.x > 0.)	// right upper corner
		pointPhi = M_PI / 2. - pointPhi;
	if (point.y > 0. && point.x > 0.)	// right lower corner
		pointPhi = M_PI / 2 - pointPhi;
	if (point.y > 0. && point.x < 0.)	// left lower corner
		pointPhi = (M_PI * 3. / 2. - pointPhi);
	if (point.y < 0. && point.x < 0.)	// left upper corner
		pointPhi = 3. / 2. * M_PI - pointPhi;
	return pointPhi;
}


/**
 * @brief Tests whether @a point falls within a hand's hit zone.
 *
 * The hit zone is bounded radially (within @a radius) and angularly
 * (within DRAG_DELTA_PHI of the hand at @a ticks tick marks past 12).
 *
 * @param point  Click location in view coordinates.
 * @param ticks  Hand position expressed in 60ths of a circle past 12.
 * @param radius Maximum distance from the clock center to count as a hit.
 * @return True when the point hits the hand.
 */
bool
TAnalogClock::_InHand(BPoint point, int32 ticks, float radius)
{
	point.x -= fCenterX;
	point.y -= fCenterY;

	float pRadius = sqrt(pow(point.x, 2) + pow(point.y, 2));

	if (radius < pRadius)
		return false;

	float pointPhi = _GetPhi(point);
	float handPhi = M_PI / 30.0 * ticks;
	float delta = pointPhi - handPhi;
	if (fabs(delta) > DRAG_DELTA_PHI)
		return false;

	return true;
}


/**
 * @brief Draws the hour, minute, and (optionally) second hands plus knob.
 *
 * Uses sin/cos to project hand endpoints onto the dial. Called twice per
 * frame: once with offset coordinates and a shadow color, once with the
 * normal coordinates and the visible colors.
 *
 * @param x            Center X (possibly offset for shadow pass).
 * @param y            Center Y (possibly offset for shadow pass).
 * @param radius       Dial radius.
 * @param hourColor    Color for the hour hand.
 * @param minuteColor  Color for the minute hand.
 * @param secondsColor Color for the second hand.
 * @param knobColor    Color for the center knob.
 */
void
TAnalogClock::_DrawHands(float x, float y, float radius,
	rgb_color hourColor, rgb_color minuteColor,
	rgb_color secondsColor, rgb_color knobColor)
{
	float offsetX;
	float offsetY;

	// calc, draw hour hand
	SetHighColor(hourColor);
	SetPenSize(4.0);
	float hours = fHours + float(fMinutes) / 60.0;
	offsetX = (radius * 0.7) * sinf((hours * M_PI) / 6.0);
	offsetY = (radius * 0.7) * cosf((hours * M_PI) / 6.0);
	StrokeLine(BPoint(x, y), BPoint(x + offsetX, y - offsetY));

	// calc, draw minute hand
	SetHighColor(minuteColor);
	SetPenSize(3.0);
	float minutes = fMinutes + float(fSeconds) / 60.0;
	offsetX = (radius * 0.9) * sinf((minutes * M_PI) / 30.0);
	offsetY = (radius * 0.9) * cosf((minutes * M_PI) / 30.0);
	StrokeLine(BPoint(x, y), BPoint(x + offsetX, y - offsetY));

	if (fDrawSecondHand) {
		// calc, draw second hand
		SetHighColor(secondsColor);
		SetPenSize(1.0);
		offsetX = (radius * 0.95) * sinf((fSeconds * M_PI) / 30.0);
		offsetY = (radius * 0.95) * cosf((fSeconds * M_PI) / 30.0);
		StrokeLine(BPoint(x, y), BPoint(x + offsetX, y - offsetY));
	}

	// draw the center knob
	SetHighColor(knobColor);
	FillEllipse(BPoint(x, y), radius * 0.06, radius * 0.06);
}
