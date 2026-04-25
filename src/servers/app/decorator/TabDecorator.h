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
 * MIT License. Copyright 2001-2020, Haiku.
 * Original authors: Stephan Aßmus, DarkWyrm, Ryan Leavengood,
 *                   Philippe Saint-Pierre, John Scipione, Ingo Weinhold,
 *                   Clemens Zeidler, Joseph Groover, Jacob Secunda.
 */

/** @file TabDecorator.h
    @brief Abstract Decorator subclass that lays out a tabbed title bar and
           a resizable border frame; concrete subclasses supply the painting. */

#ifndef TAB_DECORATOR_H
#define TAB_DECORATOR_H


#include <Region.h>

#include "Decorator.h"


class Desktop;


/** @brief Decorator base class that handles tab-bar layout, multi-tab
           stacking, border geometry, and resize-knob hit-testing. The actual
           drawing routines remain pure virtual for subclasses such as
           DefaultDecorator. */
class TabDecorator: public Decorator {
public:
								TabDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~TabDecorator();

protected:
			enum {
				COLOR_TAB_FRAME_LIGHT	= 0,
				COLOR_TAB_FRAME_DARK	= 1,
				COLOR_TAB				= 2,
				COLOR_TAB_LIGHT			= 3,
				COLOR_TAB_BEVEL			= 4,
				COLOR_TAB_SHADOW		= 5,
				COLOR_TAB_TEXT			= 6
			};

			enum {
				COLOR_BUTTON			= 0,
				COLOR_BUTTON_LIGHT		= 1
			};

			enum Component {
				COMPONENT_TAB,

				COMPONENT_CLOSE_BUTTON,
				COMPONENT_ZOOM_BUTTON,

				COMPONENT_LEFT_BORDER,
				COMPONENT_RIGHT_BORDER,
				COMPONENT_TOP_BORDER,
				COMPONENT_BOTTOM_BORDER,

				COMPONENT_RESIZE_CORNER
			};

			typedef rgb_color ComponentColors[7];

public:
	virtual	void				Draw(BRect updateRect);
	virtual	void				Draw();

	virtual	Region				RegionAt(BPoint where, int32& tab) const;

	virtual	bool				SetRegionHighlight(Region region,
									uint8 highlight, BRegion* dirty,
									int32 tab = -1);

	virtual void				UpdateColors(DesktopSettings& settings);

protected:
	virtual	void				_DoLayout();
	virtual	void				_DoOutlineLayout();
	virtual	void				_DoTabLayout();
			void				_DistributeTabSize(float delta);

	virtual	void				_DrawFrame(BRect rect) = 0;
	virtual	void				_DrawOutlineFrame(BRect rect);
	virtual	void				_DrawTab(Decorator::Tab* tab, BRect r) = 0;

	virtual	void				_DrawButtons(Decorator::Tab* tab,
									const BRect& invalid);
	virtual	void				_DrawClose(Decorator::Tab* tab, bool direct,
									BRect r) = 0;
	virtual	void				_DrawTitle(Decorator::Tab* tab, BRect r) = 0;
	virtual	void				_DrawZoom(Decorator::Tab* tab, bool direct,
									BRect r) = 0;

	virtual	void				_SetTitle(Decorator::Tab* tab,
									const char* string,
									BRegion* updateRegion = NULL);

	virtual	void				_MoveBy(BPoint offset);
	virtual	void				_ResizeBy(BPoint offset, BRegion* dirty);

	virtual	void				_SetFocus(Decorator::Tab* tab);
	virtual	bool				_SetTabLocation(Decorator::Tab* tab,
									float location, bool isShifting,
									BRegion* updateRegion = NULL);

	virtual	bool				_SetSettings(const BMessage& settings,
									BRegion* updateRegion = NULL);

	virtual	bool				_AddTab(DesktopSettings& settings,
									int32 index = -1,
									BRegion* updateRegion = NULL);
	virtual	bool				_RemoveTab(int32 index,
									BRegion* updateRegion = NULL);
	virtual	bool				_MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);

	virtual	void				_GetFootprint(BRegion* region);

	virtual	void				_GetButtonSizeAndOffset(const BRect& tabRect,
									float* offset, float* size,
									float* inset) const;

	virtual	void				_UpdateFont(DesktopSettings& settings);

private:
			void				_LayoutTabItems(Decorator::Tab* tab,
									const BRect& tabRect);

protected:
	inline	float				_DefaultTextOffset() const;
	inline	float				_SingleTabOffsetAndSize(float& tabSize);

			void				_CalculateTabsRegion();

protected:
			BRegion				fTabsRegion;
			BRect				fOldMovingTab;
			float				fBorderResizeLength, fResizeKnobSize;

			rgb_color			fFocusFrameColor;

			rgb_color			fFocusTabColor;
			rgb_color			fFocusTabColorLight;
			rgb_color			fFocusTabColorBevel;
			rgb_color			fFocusTabColorShadow;
			rgb_color			fFocusTextColor;

			rgb_color			fNonFocusFrameColor;

			rgb_color			fNonFocusTabColor;
			rgb_color			fNonFocusTabColorLight;
			rgb_color			fNonFocusTabColorBevel;
			rgb_color			fNonFocusTabColorShadow;
			rgb_color			fNonFocusTextColor;
};


#endif	// TAB_DECORATOR_H
