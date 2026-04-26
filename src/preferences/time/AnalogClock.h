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
 * MIT License. Copyright 2004-2011, Haiku, Inc.
 * Original authors: Mike Berg, Julun, Hamish Morrison.
 */

/** @file AnalogClock.h
    @brief Interactive analog clock view used on the Date and time tab. */

#ifndef _ANALOG_CLOCK_H
#define _ANALOG_CLOCK_H


#include <View.h>


/**
 * @brief Analog clock face with optional drag-to-set hour and minute hands.
 *
 * Tracks the current time via SetTime(), can render a second hand, and
 * exposes hit-testing helpers so the surrounding view can drag the hour
 * and minute hands around the dial. While a drag is in progress, automatic
 * SetTime() updates are suppressed.
 */
class TAnalogClock : public BView {
public:
							TAnalogClock(const char* name,
								bool drawSecondHand = true,
								bool interactive = true);
	virtual					~TAnalogClock();

	virtual	void			Draw(BRect updateRect);
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			MouseDown(BPoint point);
	virtual	void			MouseUp(BPoint point);
	virtual	void			MouseMoved(BPoint point, uint32 transit,
								const BMessage* message);
	virtual	void			DoLayout();

	virtual	BSize			MaxSize();
	virtual	BSize			MinSize();
	virtual	BSize			PreferredSize();

			void			SetTime(int32 hour, int32 minute, int32 second);
			bool			IsChangingTime();
			void			ChangeTimeFinished();

			void 			GetTime(int32* hour, int32* minute, int32* second);
			void 			DrawClock();

			bool			InHourHand(BPoint point);
			bool			InMinuteHand(BPoint point);

			void			SetHourHand(BPoint point);
			void			SetMinuteHand(BPoint point);

			void			SetHourDragging(bool dragging);
			void			SetMinuteDragging(bool dragging);
private:
			float			_GetPhi(BPoint point);
			bool			_InHand(BPoint point, int32 ticks, float radius);
			void			_DrawHands(float x, float y, float radius,
								rgb_color hourHourColor,
								rgb_color hourMinuteColor,
								rgb_color secondsColor, rgb_color knobColor);

			int32			fHours;
			int32			fMinutes;
			int32			fSeconds;
			bool			fDirty;

			float 			fCenterX;
			float			fCenterY;
			float			fRadius;

			bool			fHourDragging;
			bool			fMinuteDragging;
			bool			fDrawSecondHand;
			bool			fInteractive;

			bool			fTimeChangeIsOngoing;
};


#endif	// _ANALOG_CLOCK_H
