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
 * MIT License. Copyright 2010-2012 Haiku, Inc.
 * Original authors: DarkWyrm, John Scipione.
 */

/** @file FakeScrollBar.h
    @brief Static scroll-bar preview used in the Look-and-feel tab. */

#ifndef FAKE_SCROLL_BAR_H
#define FAKE_SCROLL_BAR_H


#include <Control.h>


/**
 * @brief BControl subclass that displays a non-functional scroll-bar preview.
 *
 * Used as a radio-group element so the user can pick between single-
 * and double-arrow scroll bars by clicking on the preview itself.
 */
class FakeScrollBar : public BControl {
public:
							FakeScrollBar(bool drawArrows, bool doubleArrows,
								BMessage* message);
							~FakeScrollBar(void);

	virtual	void			MouseDown(BPoint point);
	virtual	void			MouseMoved(BPoint point, uint32 transit,
								const BMessage *message);
	virtual	void			MouseUp(BPoint point);

	virtual	void			Draw(BRect updateRect);

	virtual	void			SetValue(int32 value);

			void			SetDoubleArrows(bool doubleArrows);
			void			SetKnobStyle(uint32 knobStyle);

			void			SetFromScrollBarInfo(const scroll_bar_info &info);

private:
			void			_DrawArrowButton(int32 direction, BRect r,
								const BRect& updateRect);

			bool			fDrawArrows;
			bool			fDoubleArrows;
			int32			fKnobStyle;
};

#endif	// FAKE_SCROLL_BAR_H
