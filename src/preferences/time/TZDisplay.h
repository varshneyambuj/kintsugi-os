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
 * Original authors: Andrew McCall, Mike Berg, Julun, Hamish Morrison.
 */

/** @file TZDisplay.h
    @brief Two-line label / city / time display widget for the Time Zone tab. */

#ifndef _TZ_DISPLAY_H
#define _TZ_DISPLAY_H


#include <String.h>
#include <View.h>
#include <stdio.h>


/**
 * @brief Read-only widget showing a label, a city name, and a time string.
 *
 * The label and time are placed on the first line (label left, time right),
 * and the city/zone description sits on the second line. Used to render the
 * "Current time" and "Preview time" panels on the time-zone tab.
 */
class TTZDisplay : public BView {
public:
								TTZDisplay(const char* name,
									const char* label);
	virtual						~TTZDisplay();

	virtual	void				AttachedToWindow();
	virtual	void				ResizeToPreferred();
	virtual	void				Draw(BRect updateRect);

	virtual BSize				MaxSize();
	virtual	BSize				MinSize();
	virtual	BSize				PreferredSize();

			const char*			Label() const;
			void				SetLabel(const char* label);

			const char*			Text() const;
			void				SetText(const char* text);

			const char*			Time() const;
			void				SetTime(const char* time);

private:
			BSize				_CalcPrefSize();

			BString 			fLabel;
			BString 			fText;
			BString 			fTime;
};


#endif	// _TZ_DISPLAY_H

