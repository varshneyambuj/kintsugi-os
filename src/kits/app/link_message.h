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
 * MIT License. Copyright 2005, Haiku.
 * Original authors: Axel Dörfler.
 */

/** @file link_message.h
    @brief Wire-format constants and the message_header layout shared by
           LinkSender and LinkReceiver. */

#ifndef _LINK_MESSAGE_H_
#define _LINK_MESSAGE_H_


#include <SupportDefs.h>


/** @brief Magic code stamped on every PortLink message buffer ('_PTL'). */
static const int32 kLinkCode = '_PTL';

/** @brief Initial allocation size (in bytes) for a LinkSender's send buffer. */
static const size_t kInitialBufferSize = 2048;
/** @brief Hard upper bound on a single PortLink message; larger payloads must
 *         use a different transport (e.g. shared areas). */
static const size_t kMaxBufferSize = 65536;
	// anything beyond that should be sent with a different mechanism

/** @brief On-the-wire header prepended to every PortLink message. */
struct message_header {
	int32	size;	/**< @brief Total message size in bytes, including header. */
	uint32	code;	/**< @brief Application-defined message code. */
	uint32	flags;	/**< @brief Bit flags such as @c kNeedsReply. */
};

/** @brief Flag bit in message_header::flags requesting a synchronous reply. */
static const uint32 kNeedsReply = 0x01;

#endif	/* _LINK_MESSAGE_H_ */
