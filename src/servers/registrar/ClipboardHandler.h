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
   ClipboardHandler.h
 */

/** @file ClipboardHandler.h
 *  @brief BHandler subclass that routes clipboard-related messages to named Clipboard instances. */
#ifndef CLIPBOARD_HANDLER_H
#define CLIPBOARD_HANDLER_H

#include <Handler.h>
#include <Message.h>

class Clipboard;

/** @brief Receives and dispatches clipboard BMessages to the appropriate Clipboard object. */
class ClipboardHandler : public BHandler {
public:
	ClipboardHandler();
	virtual ~ClipboardHandler();

	/** @brief Processes incoming clipboard operation messages. */
	virtual void MessageReceived(BMessage *message);

private:
	Clipboard *_GetClipboard(const char *name);

	struct ClipboardMap;

	ClipboardMap	*fClipboards;
};

#endif	// CLIPBOARD_HANDLER_H

