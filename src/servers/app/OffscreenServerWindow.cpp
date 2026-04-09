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
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */

/** @file OffscreenServerWindow.cpp
 *  @brief ServerWindow specialisation for off-screen bitmap rendering without a client thread.
 */

#include "OffscreenWindow.h"
#include "ServerBitmap.h"

#include "OffscreenServerWindow.h"


/** @brief Constructs an OffscreenServerWindow backed by the given bitmap.
 *
 *  Delegates to the base ServerWindow constructor and retains a reference to
 *  \a bitmap which will be used when MakeWindow() is called to create the
 *  associated OffscreenWindow.
 *
 *  @param title       The window title string.
 *  @param app         The ServerApp that owns this window.
 *  @param clientPort  The port used to receive messages from the client side.
 *  @param looperPort  The port of the client BLooper.
 *  @param handlerID   The handler token for this window on the client side.
 *  @param bitmap      The target bitmap into which all rendering will be directed.
 */
OffscreenServerWindow::OffscreenServerWindow(const char *title, ServerApp *app,
		port_id clientPort, port_id looperPort, int32 handlerID,
		ServerBitmap* bitmap)
	: ServerWindow(title, app, clientPort, looperPort, handlerID),
	fBitmap(bitmap, true)
{
}


/** @brief Destructor. */
OffscreenServerWindow::~OffscreenServerWindow()
{
}


/** @brief No-op override that suppresses message delivery to the client.
 *
 *  Offscreen windows have no running client BWindow thread, so posting
 *  messages would be meaningless or harmful.  This override silently
 *  discards all messages instead of forwarding them.
 *
 *  @param msg         The message that would normally be sent to the client.
 *  @param target      The token of the target handler (unused).
 *  @param usePreferred Whether to use the preferred handler (unused).
 */
void
OffscreenServerWindow::SendMessageToClient(const BMessage* msg, int32 target,
	bool usePreferred) const
{
	// We're a special kind of window. The client BWindow thread is not running,
	// so we cannot post messages to the client. In order to not mess arround
	// with all the other code, we simply make this function virtual and
	// don't do anything in this implementation.
}


/** @brief Factory method that creates the OffscreenWindow for this server window.
 *
 *  Ignores the frame, look, feel, flags, and workspace parameters because the
 *  window geometry is entirely determined by the backing bitmap.
 *
 *  @param frame      Ignored; geometry comes from the bitmap bounds.
 *  @param name       The window name.
 *  @param look       Ignored.
 *  @param feel       Ignored.
 *  @param flags      Ignored.
 *  @param workspace  Ignored.
 *  @return A newly allocated OffscreenWindow, or NULL on allocation failure.
 */
Window*
OffscreenServerWindow::MakeWindow(BRect frame, const char* name,
	window_look look, window_feel feel, uint32 flags, uint32 workspace)
{
	return new OffscreenWindow(fBitmap, name, this);
}
