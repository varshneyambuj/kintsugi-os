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
 *   Copyright 2007-2015, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */
#ifndef _MESSAGE_ADAPTER_H_
#define _MESSAGE_ADAPTER_H_


#include <Message.h>
#include <kernel/util/KMessage.h>


// message formats
#define MESSAGE_FORMAT_R5				'FOB1'
#define MESSAGE_FORMAT_R5_SWAPPED		'1BOF'
#define MESSAGE_FORMAT_DANO				'FOB2'
#define MESSAGE_FORMAT_DANO_SWAPPED		'2BOF'
#define MESSAGE_FORMAT_HAIKU			'1FMH'
#define MESSAGE_FORMAT_HAIKU_SWAPPED	'HMF1'


namespace BPrivate {


class MessageAdapter {
public:
	static	ssize_t				FlattenedSize(uint32 format,
									const BMessage* from);

	static	status_t			Flatten(uint32 format, const BMessage* from,
									char* buffer, ssize_t* size);
	static	status_t			Flatten(uint32 format, const BMessage* from,
									BDataIO* stream, ssize_t* size);

	static	status_t			Unflatten(uint32 format, BMessage* into,
									const char* buffer);
	static	status_t			Unflatten(uint32 format, BMessage* into,
									BDataIO* stream);

	static	status_t			ConvertToKMessage(const BMessage* from,
									KMessage& to);

private:
	static	status_t			_ConvertFromKMessage(const KMessage* from,
									BMessage* to);

	static	ssize_t				_R5FlattenedSize(const BMessage* from);

	static	status_t			_FlattenR5Message(uint32 format,
									const BMessage* from, char* buffer,
									ssize_t* size);

	static	status_t			_UnflattenR5Message(uint32 format,
									BMessage* into, BDataIO* stream);
	static	status_t			_UnflattenDanoMessage(uint32 format,
									BMessage* into, BDataIO* stream);
};


} // namespace BPrivate


#endif // _MESSAGE_ADAPTER_H_
