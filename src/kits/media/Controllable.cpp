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
 *   Copyright 2009-2012, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT license.
 *
 *   Copyright (c) 2002, 2003 Marcus Overhagen <Marcus@Overhagen.de>
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files or portions
 *   thereof (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software,
 *   and to permit persons to whom the Software is furnished to do so, subject
 *   to the following conditions:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above copyright notice,
 *      in the  binary, as well as this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided with
 *      the distribution.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *   THE SOFTWARE.
 */

/** @file Controllable.cpp
 *  @brief Implements BControllable, the mix-in class that gives media nodes
 *         a parameter web and IPC handlers for get/set parameter operations.
 */

#include <Controllable.h>

#include <string.h>

#include <OS.h>
#include <ParameterWeb.h>
#include <Roster.h>
#include <TimeSource.h>

#include <MediaDebug.h>
#include <DataExchange.h>
#include <Notifications.h>


namespace BPrivate { namespace media {

/**
 * @brief Helper class that transparently handles both small inline and large
 *        area-based parameter data transfers from the media server.
 */
class ReceiveTransfer {
public:
	/**
	 * @brief Construct a ReceiveTransfer, cloning an area for large transfers.
	 *
	 * @param request      The area_request_data describing the transfer.
	 * @param smallBuffer  Fallback inline buffer for small transfers.
	 */
	ReceiveTransfer(const area_request_data& request, const void* smallBuffer)
	{
		if (request.area == -1 && smallBuffer != NULL) {
			// small data transfer uses buffer in reply
			fArea = -1;
			fData = const_cast<void*>(smallBuffer);
				// The caller is actually responsible to enforce the const;
				// we don't touch the data.
		} else {
			// large data transfer, clone area
			fArea = clone_area("get parameter data clone", &fData,
				B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, request.area);
			if (fArea < B_OK) {
				ERROR("BControllabe: cloning area failed: %s\n",
					strerror(fArea));
				fData = NULL;
			}
		}
	}

	/** @brief Destructor. Deletes the cloned area if one was created. */
	~ReceiveTransfer()
	{
		if (fArea >= B_OK)
			delete_area(fArea);
	}

	/**
	 * @brief Return B_OK if the transfer buffer is valid, or an error code.
	 *
	 * @return B_OK or a negative error code from clone_area().
	 */
	status_t InitCheck() const
	{
		return fData != NULL ? B_OK : fArea;
	}

	/**
	 * @brief Return a pointer to the transfer buffer.
	 *
	 * @return Pointer to the usable data region.
	 */
	void* Data() const
	{
		return fData;
	}

private:
	area_id				fArea;
	void*				fData;
};

} // namespace media
} // namespace BPrivate

using BPrivate::media::ReceiveTransfer;


//	#pragma mark - protected


/**
 * @brief Destructor. Destroys the locking semaphore and the parameter web.
 */
BControllable::~BControllable()
{
	CALLED();
	if (fSem > 0)
		delete_sem(fSem);

	delete fWeb;
}


//	#pragma mark - public


/**
 * @brief Return the current parameter web associated with this node.
 *
 * @return Pointer to the BParameterWeb, or NULL if none has been set.
 */
BParameterWeb*
BControllable::Web()
{
	CALLED();
	return fWeb;
}


/**
 * @brief Acquire the parameter web lock (benaphore-style).
 *
 * @return true if the lock was acquired, false if the semaphore is invalid.
 */
bool
BControllable::LockParameterWeb()
{
	CALLED();
	if (fSem <= 0)
		return false;

	if (atomic_add(&fBen, 1) > 0) {
		status_t status;
		do {
			status = acquire_sem(fSem);
		} while (status == B_INTERRUPTED);

		return status == B_OK;
	}

	return true;
}


/**
 * @brief Release the parameter web lock (benaphore-style).
 */
void
BControllable::UnlockParameterWeb()
{
	CALLED();
	if (fSem <= 0)
		return;

	if (atomic_add(&fBen, -1) > 1)
		release_sem(fSem);
}


//	#pragma mark - protected


/**
 * @brief Protected constructor. Registers the B_CONTROLLABLE node kind and
 *        initialises the locking semaphore.
 */
BControllable::BControllable()
	: BMediaNode("this one is never called"),
	fWeb(NULL),
	fSem(create_sem(0, "BControllable lock")),
	fBen(0)
{
	CALLED();

	AddNodeKind(B_CONTROLLABLE);
}


/**
 * @brief Install a new parameter web and notify interested parties.
 *
 * The old web is deleted after the new one has been installed. A
 * B_MEDIA_WEB_CHANGED notification is sent if the web actually changed.
 *
 * @param web  New BParameterWeb to install; ownership is transferred.
 * @return B_OK always.
 */
status_t
BControllable::SetParameterWeb(BParameterWeb* web)
{
	CALLED();

	LockParameterWeb();
	BParameterWeb* old = fWeb;
	fWeb = web;

	if (fWeb != NULL) {
		// initialize BParameterWeb member variable
		fWeb->fNode = Node();
	}

	UnlockParameterWeb();

	if (old != web && web != NULL)
		BPrivate::media::notifications::WebChanged(Node());
	delete old;
	return B_OK;
}


/**
 * @brief Dispatch incoming IPC messages related to parameter get/set operations.
 *
 * Handles CONTROLLABLE_GET_PARAMETER_DATA, CONTROLLABLE_SET_PARAMETER_DATA,
 * CONTROLLABLE_GET_PARAMETER_WEB, and CONTROLLABLE_START_CONTROL_PANEL.
 *
 * @param message  The IPC message code.
 * @param data     Pointer to the message data.
 * @param size     Size of the message data in bytes.
 * @return B_OK if the message was handled, B_ERROR for unknown messages.
 */
status_t
BControllable::HandleMessage(int32 message, const void* data, size_t size)
{
	PRINT(4, "BControllable::HandleMessage %#lx, node %ld\n", message, ID());

	switch (message) {
		case CONTROLLABLE_GET_PARAMETER_DATA:
		{
			const controllable_get_parameter_data_request& request
				= *static_cast<const controllable_get_parameter_data_request*>(
					data);
			controllable_get_parameter_data_reply reply;

			ReceiveTransfer transfer(request, reply.raw_data);
			if (transfer.InitCheck() != B_OK) {
				request.SendReply(transfer.InitCheck(), &reply, sizeof(reply));
				return B_OK;
			}

			reply.size = request.request_size;
			status_t status = GetParameterValue(request.parameter_id,
				&reply.last_change, transfer.Data(), &reply.size);

			request.SendReply(status, &reply, sizeof(reply));
			return B_OK;
		}

		case CONTROLLABLE_SET_PARAMETER_DATA:
		{
			const controllable_set_parameter_data_request& request
				= *static_cast<const controllable_set_parameter_data_request*>(
					data);
			controllable_set_parameter_data_reply reply;

			ReceiveTransfer transfer(request, request.raw_data);
			if (transfer.InitCheck() != B_OK) {
				request.SendReply(transfer.InitCheck(), &reply, sizeof(reply));
				return B_OK;
			}

			// NOTE: This is not very fair, but the alternative
			// would have been to mess with friends classes and
			// member variables.
			bigtime_t perfTime = 0;
			if (request.when == -1)
				perfTime = TimeSource()->Now();
			else
				perfTime = request.when;

			SetParameterValue(request.parameter_id, perfTime,
				transfer.Data(), request.size);
			request.SendReply(B_OK, &reply, sizeof(reply));
			return B_OK;
		}

		case CONTROLLABLE_GET_PARAMETER_WEB:
		{
			const controllable_get_parameter_web_request& request
				= *static_cast<const controllable_get_parameter_web_request*>(
					data);
			controllable_get_parameter_web_reply reply;

			status_t status = B_OK;
			bool wasLocked = true;
			if (!LockParameterWeb()) {
				status = B_ERROR;
				wasLocked = false;
			}

			if (status == B_OK && fWeb != NULL) {
				if (fWeb->FlattenedSize() > request.max_size) {
					// parameter web too large
					reply.code = 0;
					reply.size = -1;
					status = B_OK;
				} else {
					ReceiveTransfer transfer(request, NULL);
					status = transfer.InitCheck();
					if (status == B_OK) {
						reply.code = fWeb->TypeCode();
						reply.size = fWeb->FlattenedSize();
						status = fWeb->Flatten(transfer.Data(), reply.size);
						if (status != B_OK) {
							ERROR("BControllable::HandleMessage "
								"CONTROLLABLE_GET_PARAMETER_WEB Flatten failed\n");
#if 0
						} else {
							printf("BControllable::HandleMessage CONTROLLABLE_GET_PARAMETER_WEB %ld bytes, 0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx\n",
								reply.size, ((uint32*)buffer)[0], ((uint32*)buffer)[1], ((uint32*)buffer)[2], ((uint32*)buffer)[3]);
#endif
						}
					}
				}
			} else {
				// no parameter web
				reply.code = 0;
				reply.size = 0;
			}
			if (wasLocked)
				UnlockParameterWeb();

			request.SendReply(status, &reply, sizeof(reply));
			return B_OK;
		}

		case CONTROLLABLE_START_CONTROL_PANEL:
		{
			const controllable_start_control_panel_request* request
				= static_cast<const controllable_start_control_panel_request*>(
					data);
			controllable_start_control_panel_reply reply;
			BMessenger targetMessenger;
			status_t status = StartControlPanel(&targetMessenger);
			if (status != B_OK) {
				ERROR("BControllable::HandleMessage "
					"CONTROLLABLE_START_CONTROL_PANEL failed\n");
			}
			reply.result = status;
			reply.team = targetMessenger.Team();
			request->SendReply(status, &reply, sizeof(reply));
			return B_OK;
		}

		default:
			return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Broadcast a notification that a parameter value has changed.
 *
 * @param id  The parameter ID that changed.
 * @return B_OK on success, or a notification error code.
 */
status_t
BControllable::BroadcastChangedParameter(int32 id)
{
	CALLED();
	return BPrivate::media::notifications::ParameterChanged(Node(), id);
}


/**
 * @brief Broadcast the new value of a parameter to all interested listeners.
 *
 * @param when       Performance time at which the value was set.
 * @param id         The parameter ID whose value changed.
 * @param newValue   Pointer to the new parameter value data.
 * @param valueSize  Size of the value data in bytes.
 * @return B_OK on success, or a notification error code.
 */
status_t
BControllable::BroadcastNewParameterValue(bigtime_t when, int32 id,
	void* newValue, size_t valueSize)
{
	CALLED();
	return BPrivate::media::notifications::NewParameterValue(Node(), id, when,
		newValue, valueSize);
}


/**
 * @brief Launch the add-on's control panel application for this node.
 *
 * Looks up the add-on image, obtains its path, and launches it with a
 * "node=<id>" argument so the panel can locate the correct node.
 *
 * @param _messenger  If non-NULL, receives a BMessenger targeting the launched
 *                    panel application.
 * @return B_OK on success, or B_ERROR / B_BAD_VALUE on failure.
 */
status_t
BControllable::StartControlPanel(BMessenger* _messenger)
{
	CALLED();

	int32 internalId;
	BMediaAddOn* addon = AddOn(&internalId);
	if (!addon) {
		ERROR("BControllable::StartControlPanel not instantiated per AddOn\n");
		return B_ERROR;
	}

	image_id imageID = addon->ImageID();
	image_info info;
	if (imageID <= 0 || get_image_info(imageID, &info) != B_OK) {
		ERROR("BControllable::StartControlPanel Error accessing image\n");
		return B_BAD_VALUE;
	}

	entry_ref ref;
	if (get_ref_for_path(info.name, &ref) != B_OK) {
		ERROR("BControllable::StartControlPanel Error getting ref\n");
		return B_BAD_VALUE;
	}

	// The first argument is "node=id" with id meaning the media_node_id
	char arg[32];
	snprintf(arg, sizeof(arg), "node=%d", (int)ID());

	team_id team;
	if (be_roster->Launch(&ref, 1, (const char* const*)&arg, &team) != B_OK) {
		ERROR("BControllable::StartControlPanel Error launching application\n");
		return B_BAD_VALUE;
	}
	printf("BControllable::StartControlPanel done with id: %" B_PRId32 "\n",
		team);

	if (_messenger)
		*_messenger = BMessenger(NULL, team);

	return B_OK;
}


/**
 * @brief Apply a flat parameter data blob to this node (not yet implemented).
 *
 * @param value  Pointer to the flattened parameter data.
 * @param size   Size of the data in bytes.
 * @return B_ERROR always (unimplemented).
 */
status_t
BControllable::ApplyParameterData(const void* value, size_t size)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Serialise a set of control IDs into a flat parameter data blob
 *        (not yet implemented).
 *
 * @param controls  Array of parameter IDs to serialise.
 * @param count     Number of elements in @p controls.
 * @param buffer    Destination buffer.
 * @param ioSize    In: capacity of @p buffer; out: bytes written.
 * @return B_ERROR always (unimplemented).
 */
status_t
BControllable::MakeParameterData(const int32* controls, int32 count,
	void* buffer, size_t* ioSize)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


//	#pragma mark - private


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_7(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_8(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_9(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_10(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_11(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_12(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_13(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_14(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BControllable::_Reserved_Controllable_15(void *) { return B_ERROR; }
