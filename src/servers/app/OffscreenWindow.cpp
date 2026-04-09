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
 *   Copyright 2005-2008, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Author:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

/** @file OffscreenWindow.cpp
 *  @brief Window subclass that renders into a server-side bitmap rather than a physical screen.
 */

#include "OffscreenWindow.h"

#include <new>
#include <stdio.h>

#include <Debug.h>

#include <WindowPrivate.h>

#include "BitmapHWInterface.h"
#include "DrawingEngine.h"
#include "ServerBitmap.h"

using std::nothrow;


/** @brief Constructs an OffscreenWindow backed by \a bitmap.
 *
 *  Creates a Window with the bitmap's bounds as the frame, no border
 *  (B_NO_BORDER_WINDOW_LOOK), and the special kOffscreenWindowFeel.  A new
 *  DrawingEngine and a BitmapHWInterface are allocated to direct all rendering
 *  into the bitmap's pixel buffer.  The visible, content, and visible-content
 *  regions are pre-initialised to the full bitmap bounds so the window is
 *  immediately ready for drawing without requiring a desktop layout pass.
 *
 *  @param bitmap  The ServerBitmap into which all drawing will be directed.
 *  @param name    The internal name of the window.
 *  @param window  The OffscreenServerWindow that owns this window object.
 */
OffscreenWindow::OffscreenWindow(ServerBitmap* bitmap,
		const char* name, ::ServerWindow* window)
	: Window(bitmap->Bounds(), name,
			B_NO_BORDER_WINDOW_LOOK, kOffscreenWindowFeel,
			0, 0, window, new (nothrow) DrawingEngine()),
	fBitmap(bitmap),
	fHWInterface(new (nothrow) BitmapHWInterface(fBitmap))
{
	if (!fHWInterface.IsSet() || !GetDrawingEngine())
		return;

	fHWInterface->Initialize();
	GetDrawingEngine()->SetHWInterface(fHWInterface.Get());

	fVisibleRegion.Set(fFrame);
	fVisibleContentRegion.Set(fFrame);
	fVisibleContentRegionValid = true;
	fContentRegion.Set(fFrame);
	fContentRegionValid = true;
}


/** @brief Destructor.
 *
 *  Detaches the hardware interface from the drawing engine, then shuts down
 *  the BitmapHWInterface under exclusive access to ensure no rendering is
 *  in progress when the bitmap memory is released.
 */
OffscreenWindow::~OffscreenWindow()
{
	if (GetDrawingEngine())
		GetDrawingEngine()->SetHWInterface(NULL);

	if (fHWInterface.IsSet()) {
		fHWInterface->LockExclusiveAccess();
		fHWInterface->Shutdown();
		fHWInterface->UnlockExclusiveAccess();
	}
}
