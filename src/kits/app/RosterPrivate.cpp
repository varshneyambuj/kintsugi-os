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
 *   Copyright 2001-2015, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold (bonefish@users.sf.net)
 */

/** @file RosterPrivate.cpp
 *  @brief BRoster::Private implementation for accessing private BRoster members.
 *
 *  Provides a friend-class based accessor for BRoster's private data,
 *  including methods to configure messengers, send messages to the registrar,
 *  validate messengers, and manage the global be_roster instance lifecycle.
 */

//! Access class for BRoster members


#include <RosterPrivate.h>

#include <Roster.h>

#include <locks.h>


/*!	\class BRoster::Private
	\brief Class used to access private BRoster members.

	This way, the only friend BRoster needs is this class.
*/


/*!	\brief Initializes the roster.

	\param mainMessenger A BMessenger targeting the registrar application.
	\param mimeMessenger A BMessenger targeting the MIME manager.
*/
void
BRoster::Private::SetTo(BMessenger mainMessenger, BMessenger mimeMessenger)
{
	if (fRoster != NULL) {
		fRoster->fMessenger = mainMessenger;
		fRoster->fMimeMessenger = mimeMessenger;
		fRoster->fMimeMessengerInitOnce = INIT_ONCE_INITIALIZED;
	}
}


/*!	\brief Sends a message to the registrar.

	\a mime specifies whether to send the message to the roster or to the
	MIME data base service.
	If \a reply is not \c NULL, the function waits for a reply.

	\param message The message to be sent.
	\param reply A pointer to a pre-allocated BMessage into which the reply
		   message will be copied. May be \c NULL.
	\param mime \c true, if the message should be sent to the MIME data base
		   service, \c false for the roster.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a message.
	- \c B_NO_INIT: the roster is \c NULL.
	- another error code
*/
status_t
BRoster::Private::SendTo(BMessage *message, BMessage *reply, bool mime)
{
	if (message == NULL)
		return B_BAD_VALUE;
	if (fRoster == NULL)
		return B_NO_INIT;

	const BMessenger& messenger = mime
		? fRoster->_MimeMessenger() : fRoster->fMessenger;
	if (messenger.IsTargetLocal())
		return B_BAD_VALUE;

	return reply != NULL
		? messenger.SendMessage(message, reply)
		: messenger.SendMessage(message);
}


/*!	\brief Returns whether the roster's messengers are valid.

	\a mime specifies whether to check the roster messenger or the one of
	the MIME data base service.

	\param mime \c true, if the MIME data base service messenger should be
		   checked, \c false for the roster messenger.
	\return \true, if the selected messenger is valid, \c false otherwise.
*/
bool
BRoster::Private::IsMessengerValid(bool mime) const
{
	return fRoster != NULL && (mime ? fRoster->_MimeMessenger().IsValid()
		: fRoster->fMessenger.IsValid());
}


/*!	\brief Initializes the global be_roster variable.

	Called before the global constructors are invoked.
*/
void
BRoster::Private::InitBeRoster()
{
	be_roster = new BRoster;
}


/*!	\brief Deletes the global be_roster.

	Called after the global destructors are invoked.
*/
void
BRoster::Private::DeleteBeRoster()
{
	delete be_roster;
}
