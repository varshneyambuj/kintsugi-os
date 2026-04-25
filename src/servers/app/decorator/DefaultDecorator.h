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
 *                   Clemens Zeidler, Joseph Groover, Tri-Edge AI,
 *                   Jacob Secunda.
 */

/** @file DefaultDecorator.h
    @brief Default ("yellow tab") window decorator: tab gradient, close/zoom buttons, resize knob. */

#ifndef DEFAULT_DECORATOR_H
#define DEFAULT_DECORATOR_H


#include "TabDecorator.h"


class Desktop;
class ServerBitmap;


/** @brief Concrete TabDecorator subclass providing the default app_server
           window appearance: gradient tab, beveled border frame, blended
           close/zoom buttons, and the document-look resize knob. */
class DefaultDecorator: public TabDecorator {
public:
								DefaultDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~DefaultDecorator();

	virtual	void				GetComponentColors(Component component,
									uint8 highlight, ComponentColors _colors,
									Decorator::Tab* tab = NULL);

	virtual void				UpdateColors(DesktopSettings& settings);

protected:
	virtual	void				_DrawFrame(BRect rect);

	virtual	void				_DrawTab(Decorator::Tab* tab, BRect r);
	virtual	void				_DrawTitle(Decorator::Tab* tab, BRect r);
	virtual	void				_DrawClose(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawZoom(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawMinimize(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawResizeKnob(BRect r, bool full,
									const ComponentColors& color);

private:
 			void				_DrawButtonBitmap(ServerBitmap* bitmap,
 									bool direct, BRect rect);
			void				_DrawBlendedRect(DrawingEngine *engine,
									const BRect rect, bool down,
									const ComponentColors& colors);
			ServerBitmap*		_GetBitmapForButton(Decorator::Tab* tab,
									Component item, bool down, int32 width,
									int32 height);

			void				_GetComponentColors(Component component,
									ComponentColors _colors,
									Decorator::Tab* tab = NULL);
};


#endif	// DEFAULT_DECORATOR_H
