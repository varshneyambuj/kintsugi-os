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
 * MIT License. Copyright 2003-2013, Haiku.
 * Original authors: Michael Phipps, Axel Dörfler.
 */

/** @file ScreenCornerSelector.h
    @brief BControl widget that lets the user pick a screen corner (or none). */

#ifndef SCREEN_CORNER_SELECTOR_H
#define SCREEN_CORNER_SELECTOR_H


#include <Control.h>


#include "ScreenSaverSettings.h"


/**
 * @brief Custom BControl that draws a stylized monitor and lets the user
 *        select one of its four corners (or no corner).
 *
 * The widget supports mouse and keyboard interaction. Its value is the
 * @c screen_corner enum; SetValue() coerces unknown values to NO_CORNER.
 */
class ScreenCornerSelector : public BControl {
public:
								ScreenCornerSelector(BRect frame,
									const char *name, BMessage* message,
									uint32 resizingMode);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint point);
	virtual	void				MouseUp(BPoint point);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);

	virtual	void				SetValue(int32 value);
	virtual	int32				Value();

			void				SetCorner(screen_corner corner);
			screen_corner		Corner() const;

private:
			BRect				_MonitorFrame() const;
			BRect				_InnerFrame(BRect monitorFrame) const;
			BRect				_CenterFrame(BRect innerFrame) const;
			void				_DrawStop(BRect innerFrame);
			void				_DrawArrow(BRect innerFrame);
			screen_corner		_ScreenCorner(BPoint point,
									screen_corner previous) const;

			screen_corner		fCurrentCorner;
			int32				fPreviousCorner;
};


#endif	// SCREEN_CORNER_SELECTOR_H
