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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2007, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */
#ifndef _DIRECT_MESSAGE_TARGET_H
#define _DIRECT_MESSAGE_TARGET_H


#include <MessageQueue.h>


namespace BPrivate {

class BDirectMessageTarget {
	public:
		BDirectMessageTarget();

		bool AddMessage(BMessage* message);

		void Close();
		void Acquire();
		void Release();

		BMessageQueue* Queue() { return &fQueue; }

	private:
		~BDirectMessageTarget();
		
		int32			fReferenceCount;
		BMessageQueue	fQueue;
		bool			fClosed;
};

}	// namespace BPrivate

#endif	// _DIRECT_MESSAGE_TARGET_H
