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

/** @file OffscreenServerWindow.h
    @brief ServerWindow subclass that routes drawing into an off-screen ServerBitmap. */

#ifndef OFFSCREEN_SERVER_WINDOW_H
#define OFFSCREEN_SERVER_WINDOW_H


#include "ServerWindow.h"


/** @brief A ServerWindow that renders into a ServerBitmap rather than a visible screen area. */
class OffscreenServerWindow : public ServerWindow {
public:
						/** @brief Constructs an off-screen server window backed by a bitmap.
						    @param title Window title string.
						    @param app The owning ServerApp.
						    @param clientPort Port used to send replies to the client.
						    @param looperPort Port of the client BLooper.
						    @param handlerID Handler token for the client window.
						    @param bitmap The ServerBitmap to render into. */
						OffscreenServerWindow(const char *title, ServerApp *app,
							port_id clientPort, port_id looperPort,
							int32 handlerID, ServerBitmap* bitmap);
	virtual				~OffscreenServerWindow();

			// util methods.
	/** @brief Overrides message delivery to suppress messages sent to off-screen windows.
	    @param msg The BMessage to deliver.
	    @param target Target token.
	    @param usePreferred true to use the preferred handler. */
	virtual	void		SendMessageToClient(const BMessage* msg,
							int32 target = B_NULL_TOKEN,
							bool usePreferred = false) const;

	/** @brief Creates an OffscreenWindow backed by the associated bitmap.
	    @param frame Initial frame rectangle.
	    @param name Window name.
	    @param look Window look style.
	    @param feel Window feel style.
	    @param flags Window flags.
	    @param workspace Workspace bitmask.
	    @return Pointer to the new Window object. */
	virtual	::Window*	MakeWindow(BRect frame, const char* name,
							window_look look, window_feel feel, uint32 flags,
							uint32 workspace);

private:
	BReference<ServerBitmap> fBitmap;
};

#endif	// OFFSCREEN_SERVER_WINDOW_H
