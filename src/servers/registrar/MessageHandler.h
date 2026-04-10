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
   This software is part of the Haiku distribution and is covered
   by the MIT License.
 */

/** @file MessageHandler.h
 *  @brief Abstract interface for objects that handle dispatched BMessages in the registrar. */

#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

class BMessage;

/** @brief Base class providing a virtual HandleMessage hook for BMessage processing. */
class MessageHandler {
public:
	MessageHandler();
	virtual ~MessageHandler();

	/** @brief Processes the given BMessage; subclasses override to define behavior. */
	virtual void HandleMessage(BMessage *message);
};

#endif	// MESSAGE_HANDLER_H
