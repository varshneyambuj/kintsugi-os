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
 *   Copyright (c) 2001, 2002 Haiku Project
 *   Authors: Ithamar R. Adema, Michael Pfeiffer
 *   Licensed under the MIT License.
 */


/**
 * @file BeUtils.cpp
 * @brief Miscellaneous utility helpers for the print kit.
 *
 * Provides small, broadly useful functions that fill gaps in the BeOS/Haiku
 * APIs as needed by the print subsystem. Includes add-on existence testing,
 * a generic reference-counted base class, an automatic-reply RAII wrapper,
 * and a helper for resolving the MIME type of a messaging sender.
 *
 * @see BeUtilsTranslation.cpp
 */


#include <Application.h>
#include <Bitmap.h>
#include <Messenger.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>

#include "BeUtils.h"


/**
 * @brief Test whether a named add-on exists at a known system directory.
 *
 * Constructs a full path by appending \a section and \a name to the
 * directory identified by \a which, then calls stat() to confirm the
 * file is present on disk.
 *
 * @param name     Leaf name of the add-on file to look for.
 * @param which    One of the B_*_DIRECTORY constants passed to find_directory().
 * @param section  Subdirectory component between the base directory and \a name
 *                 (e.g. "Print").
 * @param outPath  Receives the fully constructed path on success.
 * @return B_OK if the file exists, or an appropriate error code otherwise.
 */
status_t TestForAddonExistence(const char* name, directory_which which, const char* section, BPath& outPath)
{
	status_t err = B_OK;

	if ((err=find_directory(which, &outPath)) == B_OK &&
		(err=outPath.Append(section)) == B_OK &&
		(err=outPath.Append(name)) == B_OK)
	{
		struct stat buf;
		err = stat(outPath.Path(), &buf);
	}

	return err;
}

// Implementation of Object

/**
 * @brief Destroys the Object base instance.
 *
 * Virtual destructor required so that subclasses are destroyed correctly
 * through base-class pointers.
 */
Object::~Object() {
}

// Implementation of AutoReply

/**
 * @brief Constructs an AutoReply that will send a reply when destroyed.
 *
 * Takes ownership of \a sender and stores a reply message of type \a what
 * to be sent automatically when this object goes out of scope.
 *
 * @param sender  The BMessage whose ReturnAddress will receive the reply.
 *                Ownership is transferred; the object is deleted on destruction.
 * @param what    The message constant to use for the reply.
 */
AutoReply::AutoReply(BMessage* sender, uint32 what)
	: fSender(sender)
	, fReply(what)
{
}

/**
 * @brief Sends the stored reply and deletes the sender message.
 *
 * Delivers the reply constructed in the constructor to the sender's return
 * address and then frees the sender BMessage.
 */
AutoReply::~AutoReply() {
	fSender->SendReply(&fReply);
	delete fSender;
}

/**
 * @brief Retrieves the MIME signature of the application that sent a message.
 *
 * Uses the return address embedded in \a sender to locate the sending team in
 * the roster, then copies its application signature into \a mime.
 *
 * @param sender  The incoming BMessage whose return address identifies the sender.
 * @param mime    Receives the MIME application signature string on success.
 * @return true if the signature was found, false otherwise.
 */
bool MimeTypeForSender(BMessage* sender, BString& mime) {
	BMessenger msgr = sender->ReturnAddress();
	team_id team = msgr.Team();
	app_info info;
	if (be_roster->GetRunningAppInfo(team, &info) == B_OK) {
		mime = info.signature;
		return true;
	}
	return false;
}
