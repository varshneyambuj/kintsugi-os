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
 * MIT License. Copyright 2001-2009, Haiku. Copyright 2002, Thomas Kurschel.
 * Original authors: Rafael Romo, Thomas Kurschel,
 *                   Axel Doerfler (axeld@pinc-software.de).
 */

/** @file MonitorView.h
    @brief Schematic monitor preview shown in the Screen preferences window. */

#ifndef MONITOR_VIEW_H
#define MONITOR_VIEW_H


#include <View.h>


/**
 * @brief Stylized monitor view that previews the current desktop dimensions.
 *
 * Draws a rounded rectangle representing the physical screen, with an
 * inner rectangle scaled to the chosen resolution and tinted with the
 * desktop color. Clicking on the view launches the Backgrounds app.
 * The DPI label is computed from the EDID monitor info when available.
 */
class MonitorView : public BView {
public:
							MonitorView(BRect frame, const char* name,
								int32 screenWidth, int32 screenHeight);
	virtual					~MonitorView();

	virtual	void			AttachedToWindow();
	virtual	void			Draw(BRect updateRect);
	virtual	void			MessageReceived(BMessage *message);
	virtual	void			MouseDown(BPoint point);

			void			SetResolution(int32 width, int32 height);
			void			SetMaxResolution(int32 width, int32 height);

private:
			BRect			_MonitorBounds();
			void			_UpdateDPI();

			rgb_color		fBackgroundColor;
			rgb_color		fDesktopColor;
			int32			fMaxWidth;
			int32			fMaxHeight;
			int32			fWidth;
			int32			fHeight;
			int32			fDPI;
};

#endif	/* MONITOR_VIEW_H */
