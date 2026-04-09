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
 * MIT License. Copyright 2005-2008, Haiku, Inc. All rights reserved.
 * Original author: Stephan Aßmus <superstippi@gmx.de>.
 */

/** @file OffscreenWindow.h
    @brief Window subclass that renders into a ServerBitmap via a BitmapHWInterface. */

#ifndef OFFSCREEN_WINDOW_H
#define OFFSCREEN_WINDOW_H


#include "Window.h"

#include <AutoDeleter.h>


class BitmapHWInterface;
class ServerBitmap;

/** @brief A Window backed by a ServerBitmap rather than a physical screen, used for
           off-screen BDirectWindow rendering. */
class OffscreenWindow : public Window {
public:
							/** @brief Constructs an off-screen window rendering into a bitmap.
							    @param bitmap The ServerBitmap to render into.
							    @param name Window name.
							    @param window The associated ServerWindow. */
							OffscreenWindow(ServerBitmap* bitmap,
								const char* name, ::ServerWindow* window);
	virtual					~OffscreenWindow();

	/** @brief Returns true, identifying this as an off-screen window.
	    @return true. */
	virtual	bool			IsOffscreenWindow() const
								{ return true; }

private:
	ServerBitmap*			fBitmap;
	ObjectDeleter<BitmapHWInterface>
							fHWInterface;
};

#endif	// OFFSCREEN_WINDOW_H
