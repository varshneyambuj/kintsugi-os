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
   Copyright 2002-2006, Haiku Inc.
   Distributed under the terms of the MIT License.

   Authors:
   Tyler Dauwalder
   Ingo Weinhold, bonefish@users.sf.net
 */

/** @file MimeUpdateThread.cpp
 *  @brief Implements the recursive directory walker for MIME database updates. */

#include "MimeUpdateThread.h"

#include <stdio.h>

#include <Directory.h>
#include <Message.h>
#include <Path.h>
#include <RegistrarDefs.h>
#include <Volume.h>

#include <storage_support.h>

//#define DBG(x) x
#define DBG(x)
#define OUT printf

namespace BPrivate {
namespace Storage {
namespace Mime {

/**
 * @brief Constructs a MimeUpdateThread for recursive MIME database updates.
 *
 * If @a replyee is non-NULL and construction succeeds, this object assumes
 * ownership and will delete it. The replyee is expected to be a synchronous
 * B_REG_MIME_UPDATE_MIME_INFO or B_REG_MIME_CREATE_APP_META_MIME message.
 *
 * @param name              Thread name.
 * @param priority          Thread scheduling priority.
 * @param database          The MIME database to operate on.
 * @param managerMessenger  Messenger for communicating with the thread manager.
 * @param root              Root entry_ref of the directory tree to scan.
 * @param recursive         Whether to recurse into subdirectories.
 * @param force             If non-zero, forces updates even for existing entries.
 * @param replyee           Optional message to reply to when the operation
 *                          completes. Ownership transfers on success.
 */
MimeUpdateThread::MimeUpdateThread(const char *name, int32 priority,
		Database *database, BMessenger managerMessenger, const entry_ref *root,
		bool recursive, int32 force, BMessage *replyee)
	:
	RegistrarThread(name, priority, managerMessenger),
	fDatabase(database),
	fRoot(root ? *root : entry_ref()),
	fRecursive(recursive),
	fForce(force),
	fReplyee(replyee),
	fStatus(root ? B_OK : B_BAD_VALUE)
{
}


/**
 * @brief Destroys the MimeUpdateThread and deletes the replyee if owned.
 */
MimeUpdateThread::~MimeUpdateThread()
{
	// delete our acquired BMessage
	if (InitCheck() == B_OK)
		delete fReplyee;
}


/**
 * @brief Returns the combined initialization status of this object and its base.
 *
 * @return @c B_OK if fully initialized, or an error code.
 */
status_t
MimeUpdateThread::InitCheck()
{
	status_t err = RegistrarThread::InitCheck();
	if (!err)
		err = fStatus;
	return err;
}


/**
 * @brief Main thread body: iterates the filesystem tree and updates entries.
 *
 * Calls UpdateEntry() starting from fRoot, sends a reply to the replyee
 * if one was provided, marks the thread as finished, and notifies the
 * thread manager.
 *
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
MimeUpdateThread::ThreadFunction()
{
	status_t err = InitCheck();

	// The registrar is using this, too, so we better make sure we
	// don't run into troubles
	try {
		// Do the updates
		if (!err)
			err = UpdateEntry(&fRoot);
	} catch (...) {
		err = B_ERROR;
	}

	// Send a reply if we have a message to reply to
	if (fReplyee) {
		BMessage reply(B_REG_RESULT);
		status_t error = reply.AddInt32("result", err);
		err = error;
		if (!err)
			err = fReplyee->SendReply(&reply);
	}

	// Flag ourselves as finished
	fIsFinished = true;
	// Notify the thread manager to make a cleanup run
	if (!err) {
		BMessage msg(B_REG_MIME_UPDATE_THREAD_FINISHED);
		status_t error = fManagerMessenger.SendMessage(&msg, (BHandler*)NULL,
			500000);
		if (error)
			OUT("WARNING: ThreadManager::ThreadEntryFunction(): Termination"
				" notification failed with error 0x%" B_PRIx32 "\n", error);
	}
	DBG(OUT("(id: %ld) exiting mime update thread with result 0x%" B_PRIx32
		"\n", find_thread(NULL), err));
	return err;
}


/**
 * @brief Checks whether the given device supports file attributes.
 *
 * Results are cached so that repeated queries for the same device avoid
 * redundant statvfs() calls. Devices that support attributes are cached at
 * the front of the list for faster lookup.
 *
 * @param device The device number to check.
 * @return @c true if the device supports attributes, @c false otherwise or
 *         on error.
 */
bool
MimeUpdateThread::DeviceSupportsAttributes(dev_t device)
{
	// See if an entry for this device already exists
	std::list< std::pair<dev_t,bool> >::iterator i;
	for (i = fAttributeSupportList.begin();
			i != fAttributeSupportList.end(); i++)
	{
		if (i->first == device)
			return i->second;
	}

	bool result = false;

	// If we get here, no such device is yet in our list,
	// so we attempt to remedy the situation
	BVolume volume;
	status_t err = volume.SetTo(device);
	if (!err) {
		result = volume.KnowsAttr();
		// devices supporting attributes are likely to be queried
		// again, devices not supporting attributes are not
		std::pair<dev_t,bool> p(device, result);
		if (result)
			fAttributeSupportList.push_front(p);
		else
			fAttributeSupportList.push_back(p);
	}

	return result;
}

/**
 * @brief Updates the given entry and optionally recurses into child entries.
 *
 * Skips entries on devices that do not support attributes. If the entry is
 * a directory and fRecursive is true, all child entries are updated as well.
 *
 * @param ref The entry to update.
 * @return @c B_OK on success, @c B_BAD_VALUE if @a ref is NULL, @c B_CANCELED
 *         if the thread was asked to exit, or another error code.
 */
status_t
MimeUpdateThread::UpdateEntry(const entry_ref *ref)
{
	status_t err = ref ? B_OK : B_BAD_VALUE;
	bool entryIsDir = false;

	// Look to see if we're being terminated
	if (!err && fShouldExit)
		err = B_CANCELED;

	// Before we update, make sure this entry lives on a device that supports
	// attributes. If not, we skip it and any of its children for
	// updates (we don't signal an error, however).

//BPath path(ref);
//printf("Updating '%s' (%s)... \n", path.Path(),
//	(DeviceSupportsAttributes(ref->device) ? "yes" : "no"));

	if (!err && (device_is_root_device(ref->device)
				|| DeviceSupportsAttributes(ref->device))) {
		// Update this entry
		if (!err) {
			// R5 appears to ignore whether or not the update succeeds.
			DoMimeUpdate(ref, &entryIsDir);
		}

		// If we're recursing and this is a directory, update
		// each of the directory's children as well
		if (!err && fRecursive && entryIsDir) {
			BDirectory dir;
			err = dir.SetTo(ref);
			if (!err) {
				entry_ref childRef;
				while (!err) {
					err = dir.GetNextRef(&childRef);
					if (err) {
						// If we've come to the end of the directory listing,
						// it's not an error.
						if (err == B_ENTRY_NOT_FOUND)
							err = B_OK;
						break;
					} else
						err = UpdateEntry(&childRef);
				}
			}
		}
	}
	return err;
}

}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate
