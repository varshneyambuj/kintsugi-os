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
 *   Copyright 2003-2005, Haiku.
 *   Distributed under the terms of the MIT License.
 */
#ifndef MESSENGER_PRIVATE_H
#define MESSENGER_PRIVATE_H


#include <Messenger.h>
#include <TokenSpace.h>


class BMessenger::Private {
	public:
		Private(BMessenger* messenger) : fMessenger(messenger) {}
		Private(BMessenger& messenger) : fMessenger(&messenger) {}

		port_id	Port()
			{ return fMessenger->fPort; }
		int32 Token()
			{ return fMessenger->fHandlerToken; }
		team_id	Team()
			{ return fMessenger->fTeam; }
		bool IsPreferredTarget()
			{ return fMessenger->fHandlerToken == B_PREFERRED_TOKEN; }

		void SetTo(team_id team, port_id port, int32 token)
			{ fMessenger->_SetTo(team, port, token); }

	private:
		BMessenger* fMessenger;
};

#endif	// MESSENGER_PRIVATE_H
