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
 * MIT License. Copyright 2004-2012, Haiku, Inc.
 * Original authors: Mike Berg, Julun, Hamish Morrison.
 */

/** @file ZoneView.h
    @brief Time-zone picker tab for the Time preflet. */

#ifndef ZONE_VIEW_H
#define ZONE_VIEW_H


#include <LayoutBuilder.h>
#include <TimeFormat.h>
#include <TimeZone.h>


class BButton;
class BMessage;
class BOutlineListView;
class BPopUpMenu;
class BRadioButton;
class BTimeZone;
class TimeZoneListItem;
class TimeZoneListView;
class TTZDisplay;


/**
 * @brief Time-zone picker tab in the Time preferences.
 *
 * Builds a hierarchical region/country/zone outline from ICU data, lets
 * the user pick a zone, and applies the choice to the locale roster and
 * the kernel. Also exposes the GMT-vs-local hardware-clock toggle and a
 * live current/preview time display.
 */
class TimeZoneView : public BGroupView {
public:
								TimeZoneView(const char* name);
	virtual						~TimeZoneView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			bool				CheckCanRevert();

protected:
	virtual void				DoLayout();

private:
			void				_UpdateDateTime(BMessage* message);

			void				_SetSystemTimeZone();

			void				_UpdatePreview();
			void				_UpdateCurrent();
			BString				_FormatTime(const BTimeZone& timeZone);

			void 				_ReadRTCSettings();
			void				_WriteRTCSettings();
			void				_UpdateGmtSettings();
			void				_ShowOrHidePreview();

			void				_InitView();
			void				_BuildZoneMenu();

			void				_Revert();

			TimeZoneListView*	fZoneList;
			BButton*			fSetZone;
			TTZDisplay*			fCurrent;
			TTZDisplay*			fPreview;
			BRadioButton*		fLocalTime;
			BRadioButton*		fGmtTime;

			int32				fLastUpdateMinute;
			bool				fUseGmtTime;
			bool				fOldUseGmtTime;

			TimeZoneListItem*	fCurrentZoneItem;
			TimeZoneListItem*	fOldZoneItem;
			bool				fInitialized;

			BTimeFormat			fTimeFormat;
};


#endif // ZONE_VIEW_H
