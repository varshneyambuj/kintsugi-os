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
 *   Copyright 2015, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Joseph Groover <looncraz@looncraz.net>
 */

/** @file DelayedMessage.cpp
 *  @brief Deferred server-protocol message delivery with optional merge semantics. */


#include "DelayedMessage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Autolock.h>
#include <String.h>

#include <LinkSender.h>
#include <ServerProtocol.h>


// DelayedMessageSender constants
static const int32 kWakeupMessage = AS_LAST_CODE + 2048;
static const int32 kExitMessage = kWakeupMessage + 1;

static const char* kName = "DMT is here for you, eventually...";
static int32 kPriority = B_URGENT_DISPLAY_PRIORITY;
static int32 kPortCapacity = 10;


//! Data attachment structure.
struct Attachment {
								Attachment(const void* data, size_t size);
								~Attachment();

			const void*			constData;
			void*				data;
			size_t				size;
};


typedef BObjectList<Attachment, true> AttachmentList;


/*!	\class ScheduledMessage
	\brief Responsible for sending of delayed message.
*/
class ScheduledMessage {
public:
								ScheduledMessage(DelayedMessage& message);
								~ScheduledMessage();

			int32				CountTargets() const;

			void				Finalize();
			bigtime_t			ScheduledTime() const;
			int32				SendMessage();
			bool				IsValid() const;
			bool				Merge(DelayedMessage& message);

			status_t			SendMessageToPort(port_id port);
			bool 				operator<(const ScheduledMessage& other) const;

			DelayedMessageData*	fData;
};


/*!	\class DelayedMessageSender DelayedMessageSender.h
	\brief Responsible for scheduling and sending of delayed messages
*/
class DelayedMessageSender {
public:
			explicit			DelayedMessageSender();
								~DelayedMessageSender();

			status_t			ScheduleMessage	(DelayedMessage& message);

			int32				CountDelayedMessages() const;
			int64				CountSentMessages() const;

private:
			void				_MessageLoop();
			int32				_SendDelayedMessages();
	static	int32				_thread_func(void* sender);
			void				_Wakeup(bigtime_t whatTime);

private:
	typedef BObjectList<ScheduledMessage, true> ScheduledList;

	mutable	BLocker				fLock;
			ScheduledList		fMessages;

			bigtime_t			fScheduledWakeup;

			int32				fWakeupRetry;
			thread_id			fThread;
			port_id				fPort;

	mutable	int64				fSentCount;
};


DelayedMessageSender gDelayedMessageSender;


/*!	\class DelayedMessageData DelayedMessageSender.h
	\brief Owns DelayedMessage data, allocates memory and copies data only
			when needed,
*/
class DelayedMessageData {
	typedef BObjectList<port_id, true> PortList;
	typedef void(*FailureCallback)(int32 code, port_id port, void* data);

public:
								DelayedMessageData(int32 code, bigtime_t delay,
									bool isSpecificTime);
								~DelayedMessageData();

			bool				AddTarget(port_id port);
			void				RemoveTarget(port_id port);
			int32				CountTargets() const;

			void				MergeTargets(DelayedMessageData* other);

			bool				CopyData();
			bool				MergeData(DelayedMessageData* other);

			bool				IsValid() const;
				// Only valid after a successful CopyData().

			status_t			Attach(const void* data, size_t size);

			bool				Compare(Attachment* one, Attachment* two,
									int32 index);

			void				SetMerge(DMMergeMode mode, uint32 mask);
			void				SendFailed(port_id port);

			void				SetFailureCallback(FailureCallback callback,
									void* data);

			// Accessors.
			int32&				Code() {return fCode;}
			const int32&		Code() const {return fCode;}

			bigtime_t&			ScheduledTime() {return fScheduledTime;}
			const bigtime_t&	ScheduledTime() const {return fScheduledTime;}

			AttachmentList&		Attachments() {return fAttachments;}
			const AttachmentList&	Attachments() const {return fAttachments;}

			PortList&			Targets() {return fTargets;}
			const PortList&		Targets() const {return fTargets;}

private:
		// Data members.

			int32				fCode;
			bigtime_t			fScheduledTime;
			bool				fValid;

			AttachmentList		fAttachments;
			PortList			fTargets;

			DMMergeMode			fMergeMode;
			uint32				fMergeMask;

			FailureCallback		fFailureCallback;
			void*				fFailureData;
};


// #pragma mark -


/**
 * @brief Constructs a DelayedMessage with the given protocol code and delay.
 *
 * The delay is clamped to DM_MINIMUM_DELAY if smaller. When @a isSpecificTime
 * is true @a delay is treated as an absolute system time; otherwise as a
 * relative offset from now.
 *
 * @param code          Server protocol message code.
 * @param delay         Delivery delay in microseconds (or absolute time).
 * @param isSpecificTime If true @a delay is an absolute timestamp.
 */
DelayedMessage::DelayedMessage(int32 code, bigtime_t delay,
		bool isSpecificTime)
	:
	fData(new(std::nothrow) DelayedMessageData(code, delay < DM_MINIMUM_DELAY
		? DM_MINIMUM_DELAY : delay, isSpecificTime)),
	fHandedOff(false)
{
}


/**
 * @brief Destroys the DelayedMessage.
 *
 * If the message was never flushed (handed off), the data is deleted here,
 * effectively canceling the message at low cost.
 */
DelayedMessage::~DelayedMessage()
{
	// Message is canceled without a handoff.
	if (!fHandedOff)
		delete fData;
}


/**
 * @brief Adds a delivery target port.
 * @param port The port_id to deliver the message to.
 * @return true if the target was added, false if already present or on error.
 */
bool
DelayedMessage::AddTarget(port_id port)
{
	if (fData == NULL || fHandedOff)
		return false;

	return fData->AddTarget(port);
}


/**
 * @brief Configures merge behavior for this message.
 * @param mode  The DMMergeMode controlling how duplicate messages are handled.
 * @param match Bitmask selecting which attachment fields must match for a merge.
 */
void
DelayedMessage::SetMerge(DMMergeMode mode, uint32 match)
{
	if (fData == NULL || fHandedOff)
		return;

	fData->SetMerge(mode, match);
}


/**
 * @brief Sets a callback to invoke when delivery to a port fails.
 * @param callback Function called with (code, port, data) on failure.
 * @param data     User data passed through to the callback.
 */
void
DelayedMessage::SetFailureCallback(void (*callback)(int32, port_id, void*),
	void* data)
{
	if (fData == NULL || fHandedOff)
		return;

	fData->SetFailureCallback(callback, data);
}


/**
 * @brief Attaches data to the message. Memory is not copied until handoff.
 * @param data Pointer to the data to attach (must remain valid until Flush()).
 * @param size Number of bytes to attach.
 * @return B_OK on success, B_NO_MEMORY, B_ERROR, or B_BAD_VALUE on failure.
 */
status_t
DelayedMessage::Attach(const void* data, size_t size)
{
	if (fData == NULL)
		return B_NO_MEMORY;

	if (fHandedOff)
		return B_ERROR;

	if (data == NULL || size == 0)
		return B_BAD_VALUE;

	return	fData->Attach(data, size);
}


/**
 * @brief Schedules the message for delivery at its configured time.
 *
 * At least one target must be set before calling Flush(). On success the
 * message data is handed off to the DelayedMessageSender and this object
 * must not be modified further.
 *
 * @return B_OK on success, B_NO_MEMORY, B_ERROR, or B_BAD_VALUE on failure.
 */
status_t
DelayedMessage::Flush()
{
	if (fData == NULL)
		return B_NO_MEMORY;

	if (fHandedOff)
		return B_ERROR;

	if (fData->CountTargets() == 0)
		return B_BAD_VALUE;

	return gDelayedMessageSender.ScheduleMessage(*this);
}


/**
 * @brief Transfers ownership of the internal data to the caller.
 *
 * Data is copied from the original source locations here. The handoff reduces
 * allocation overhead for messages that are ultimately canceled.
 *
 * @return Pointer to the DelayedMessageData on success, NULL otherwise.
 */
DelayedMessageData*
DelayedMessage::HandOff()
{
	if (fData == NULL || fHandedOff)
		return NULL;

	if (fData->CopyData()) {
		fHandedOff = true;
		return fData;
	}

	return NULL;
}


// #pragma mark -


/**
 * @brief Constructs an Attachment holding a const pointer and size.
 * @param _data Pointer to the source data (not copied yet).
 * @param _size Number of bytes the attachment covers.
 */
Attachment::Attachment(const void* _data, size_t _size)
	:
	constData(_data),
	data(NULL),
	size(_size)
{
}


/**
 * @brief Destroys the Attachment, freeing the copied data buffer if present.
 */
Attachment::~Attachment()
{
	free(data);
}


// #pragma mark -


/**
 * @brief Constructs DelayedMessageData for the given code and delivery time.
 * @param code          Server protocol code.
 * @param delay         Delivery delay or absolute time in microseconds.
 * @param isSpecificTime If true @a delay is an absolute timestamp.
 */
DelayedMessageData::DelayedMessageData(int32 code, bigtime_t delay,
	bool isSpecificTime)
	:
	fCode(code),
	fScheduledTime(delay + (isSpecificTime ? 0 : system_time())),
	fValid(false),

	fAttachments(3),
	fTargets(4),

	fMergeMode(DM_NO_MERGE),
	fMergeMask(DM_DATA_DEFAULT),

	fFailureCallback(NULL),
	fFailureData(NULL)
{
}


/**
 * @brief Destroys the DelayedMessageData.
 */
DelayedMessageData::~DelayedMessageData()
{
}


/**
 * @brief Adds a delivery target, rejecting duplicates.
 * @param port The port_id to add (must be > 0).
 * @return true if added, false if already present or invalid.
 */
bool
DelayedMessageData::AddTarget(port_id port)
{
	if (port <= 0)
		return false;

	// check for duplicates:
	for (int32 index = 0; index < fTargets.CountItems(); ++index) {
		if (port == *fTargets.ItemAt(index))
			return false;
	}

	return fTargets.AddItem(new(std::nothrow) port_id(port));
}


/**
 * @brief Removes a target port by value.
 * @param port The port_id to remove (B_BAD_PORT_ID is ignored).
 */
void
DelayedMessageData::RemoveTarget(port_id port)
{
	if (port == B_BAD_PORT_ID)
		return;

	// Search for a match by value.
	for (int32 index = 0; index < fTargets.CountItems(); ++index) {
		port_id* target = fTargets.ItemAt(index);
		if (port == *target) {
			fTargets.RemoveItem(target, true);
			return;
		}
	}
}


/**
 * @brief Returns the number of registered delivery targets.
 * @return Count of target ports.
 */
int32
DelayedMessageData::CountTargets() const
{
	return fTargets.CountItems();
}


/**
 * @brief Merges target ports from @a other into this object.
 *
 * Failure to add one target does not abort the loop; it may simply mean the
 * target is already present.
 *
 * @param other The source DelayedMessageData whose targets are merged.
 */
void
DelayedMessageData::MergeTargets(DelayedMessageData* other)
{
	// Failure to add one target does not abort the loop!
	// It could just mean we already have the target.
	for (int32 index = 0; index < other->fTargets.CountItems(); ++index)
		AddTarget(*(other->fTargets.ItemAt(index)));
}


/**
 * @brief Copies attachment data from the original source locations.
 *
 * After a successful copy, IsValid() returns true. This is called at handoff
 * time so that canceled messages have no allocation cost.
 *
 * @return true on success, false if any allocation fails.
 */
bool
DelayedMessageData::CopyData()
{
	Attachment* attached = NULL;

	for (int32 index = 0; index < fAttachments.CountItems(); ++index) {
		attached = fAttachments.ItemAt(index);

		if (attached == NULL || attached->data != NULL)
			return false;

		attached->data = malloc(attached->size);
		if (attached->data == NULL)
			return false;

		memcpy(attached->data, attached->constData, attached->size);
	}

	fValid = true;
	return true;
}


/**
 * @brief Attempts to merge @a other's data into this message.
 *
 * Merging is possible only when both messages have the same code, the same
 * merge mode (and neither is DM_NO_MERGE), and the same number of attachments.
 * DM_MERGE_CANCEL merges targets only; DM_MERGE_DUPLICATES and DM_MERGE_REPLACE
 * additionally compare and/or replace attachment data.
 *
 * @param other The DelayedMessageData to merge from.
 * @return true if the merge succeeded, false otherwise.
 */
bool
DelayedMessageData::MergeData(DelayedMessageData* other)
{
	if (!fValid
		|| other == NULL
		|| other->fCode != fCode
		|| fMergeMode == DM_NO_MERGE
		|| other->fMergeMode == DM_NO_MERGE
		|| other->fMergeMode != fMergeMode
		|| other->fAttachments.CountItems() != fAttachments.CountItems())
		return false;

	if (other->fMergeMode == DM_MERGE_CANCEL) {
		MergeTargets(other);
		return true;
	}

	// Compare data
	Attachment* attached = NULL;
	Attachment* otherAttached = NULL;

	for (int32 index = 0; index < fAttachments.CountItems(); ++index) {
		attached = fAttachments.ItemAt(index);
		otherAttached = other->fAttachments.ItemAt(index);

		if (attached == NULL
			|| otherAttached == NULL
			|| attached->data == NULL
			|| otherAttached->constData == NULL
			|| attached->size != otherAttached->size)
			return false;

		// Compares depending upon mode & flags
		if (!Compare(attached, otherAttached, index))
			return false;
	}

	// add any targets not included in the existing message!
	MergeTargets(other);

	// since these are duplicates, we need not copy anything...
	if (fMergeMode == DM_MERGE_DUPLICATES)
		return true;

	// DM_MERGE_REPLACE:

	// Import the new data!
	for (int32 index = 0; index < fAttachments.CountItems(); ++index) {
		attached = fAttachments.ItemAt(index);
		otherAttached = other->fAttachments.ItemAt(index);

		// We already have allocated our memory, but the other data
		// has not.  So this reduces memory allocations.
		memcpy(attached->data, otherAttached->constData, attached->size);
	}

	return true;
}


/**
 * @brief Returns whether this object's data has been copied and is ready for sending.
 * @return true after a successful CopyData(), false otherwise.
 */
bool
DelayedMessageData::IsValid() const
{
	return fValid;
}


/**
 * @brief Stores a pointer and size as a new attachment entry.
 * @param data Pointer to the source data (not copied yet).
 * @param size Number of bytes to attach.
 * @return B_OK on success, B_NO_MEMORY or B_ERROR on failure.
 */
status_t
DelayedMessageData::Attach(const void* data, size_t size)
{
	// Sanity checking already performed
	Attachment* attach = new(std::nothrow) Attachment(data, size);

	if (attach == NULL)
		return B_NO_MEMORY;

	if (fAttachments.AddItem(attach) == false) {
		delete attach;
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Compares two attachment slots according to the current merge mode and mask.
 * @param one   The existing (already-copied) attachment.
 * @param two   The incoming (not-yet-copied) attachment.
 * @param index Zero-based slot index used with the merge mask.
 * @return true if the attachments are considered equivalent for the current mode.
 */
bool
DelayedMessageData::Compare(Attachment* one, Attachment* two, int32 index)
{
	if (fMergeMode == DM_MERGE_DUPLICATES) {

		// Default-policy: all data must match
		if (fMergeMask == DM_DATA_DEFAULT || (fMergeMask & 1 << index) != 0)
			return memcmp(one->data, two->constData, one->size) == 0;

	} else if (fMergeMode == DM_MERGE_REPLACE) {

		// Default Policy: no data needs to match
		if (fMergeMask != DM_DATA_DEFAULT && (fMergeMask & 1 << index) != 0)
			return memcmp(one->data, two->constData, one->size) == 0;
	}

	return true;
}


/**
 * @brief Configures the merge mode and mask for this message.
 * @param mode The DMMergeMode to set.
 * @param mask Bitmask selecting attachment fields that participate in merge comparison.
 */
void
DelayedMessageData::SetMerge(DMMergeMode mode, uint32 mask)
{
	fMergeMode = mode;
	fMergeMask = mask;
}


/**
 * @brief Invokes the failure callback if one has been set.
 * @param port The port_id that failed to receive the message.
 */
void
DelayedMessageData::SendFailed(port_id port)
{
	if (fFailureCallback != NULL)
		fFailureCallback(fCode, port, fFailureData);
}


/**
 * @brief Registers a failure callback and associated user data.
 * @param callback Function to call on delivery failure.
 * @param data     User data passed through to the callback.
 */
void
DelayedMessageData::SetFailureCallback(FailureCallback callback, void* data)
{
	fFailureCallback = callback;
	fFailureData = data;
}


// #pragma mark -


/**
 * @brief Constructs a ScheduledMessage by handing off data from @a message.
 * @param message The DelayedMessage whose data is taken over.
 */
ScheduledMessage::ScheduledMessage(DelayedMessage& message)
	:
	fData(message.HandOff())
{
}


/**
 * @brief Destroys the ScheduledMessage, freeing the owned DelayedMessageData.
 */
ScheduledMessage::~ScheduledMessage()
{
	delete fData;
}


/**
 * @brief Returns the number of remaining delivery targets.
 * @return Target count, or 0 if the data is invalid.
 */
int32
ScheduledMessage::CountTargets() const
{
	if (fData == NULL)
		return 0;

	return fData->CountTargets();
}


/**
 * @brief Returns the absolute system time at which this message should be sent.
 * @return Scheduled delivery time in microseconds, or 0 if invalid.
 */
bigtime_t
ScheduledMessage::ScheduledTime() const
{
	if (fData == NULL)
		return 0;

	return fData->ScheduledTime();
}


/**
 * @brief Sends the message to all registered targets and returns the sent count.
 *
 * Targets that fail with a non-timeout error have the failure callback invoked.
 *
 * @return Number of targets successfully reached.
 */
int32
ScheduledMessage::SendMessage()
{
	if (fData == NULL || !fData->IsValid())
		return 0;

	int32 sent = 0;
	for (int32 index = 0; index < fData->Targets().CountItems(); ++index) {
		port_id port = *(fData->Targets().ItemAt(index));
		status_t error = SendMessageToPort(port);

		if (error == B_OK) {
			++sent;
			continue;
		}

		if (error != B_TIMED_OUT)
			fData->SendFailed(port);
	}

	return sent;
}


/**
 * @brief Serializes and sends the message to a single target port.
 *
 * Uses a BPrivate::LinkSender with a 1-second flush timeout. Successfully
 * sent or port-deleted targets are removed from the target list.
 *
 * @param port The destination port_id.
 * @return B_OK on success, B_BAD_VALUE for B_BAD_PORT_ID, B_BAD_DATA if invalid,
 *         or a LinkSender error code.
 */
status_t
ScheduledMessage::SendMessageToPort(port_id port)
{
	if (fData == NULL || !fData->IsValid())
		return B_BAD_DATA;

	if (port == B_BAD_PORT_ID)
		return B_BAD_VALUE;

	BPrivate::LinkSender sender(port);
	if (sender.StartMessage(fData->Code()) != B_OK)
		return B_ERROR;

	AttachmentList& list = fData->Attachments();
	Attachment* attached = NULL;
	status_t error = B_OK;

	// The data has been checked already, so we assume it is all good
	for (int32 index = 0; index < list.CountItems(); ++index) {
		attached = list.ItemAt(index);

		error = sender.Attach(attached->data, attached->size);
		if (error != B_OK) {
			sender.CancelMessage();
			return error;
		}
	}

	// We do not want to ever hold up the sender thread for too long, we
	// set a 1 second sending delay, which should be more than enough for
	// 99.992% of all cases.  Approximately.
	error = sender.Flush(1000000);

	if (error == B_OK || error == B_BAD_PORT_ID)
		fData->RemoveTarget(port);

	return error;
}


/**
 * @brief Returns whether this ScheduledMessage holds valid, sendable data.
 * @return true if the data is non-NULL and valid.
 */
bool
ScheduledMessage::IsValid() const
{
	return fData != NULL && fData->IsValid();
}


/**
 * @brief Attempts to merge the incoming @a other message into this one.
 * @param other The DelayedMessage to merge from.
 * @return true if the merge succeeded, false otherwise.
 */
bool
ScheduledMessage::Merge(DelayedMessage& other)
{
	if (!IsValid())
		return false;

	return fData->MergeData(other.Data());
}


/**
 * @brief Orders messages by scheduled delivery time (earliest first).
 * @param other The ScheduledMessage to compare with.
 * @return true if this message is scheduled earlier than @a other.
 */
bool
ScheduledMessage::operator<(const ScheduledMessage& other) const
{
	if (!IsValid() || !other.IsValid())
		return false;

	return fData->ScheduledTime() < other.fData->ScheduledTime();
}


/**
 * @brief Comparator function for sorting ScheduledMessage objects.
 * @param one First message.
 * @param two Second message.
 * @return Negative if one < two, positive otherwise.
 */
int
CompareMessages(const ScheduledMessage* one, const ScheduledMessage* two)
{
	return *one < *two;
}


// #pragma mark -


/**
 * @brief Constructs the global DelayedMessageSender and starts its worker thread.
 */
DelayedMessageSender::DelayedMessageSender()
	:
	fLock("DelayedMessageSender"),
	fMessages(20),
	fScheduledWakeup(B_INFINITE_TIMEOUT),
	fWakeupRetry(0),
	fThread(spawn_thread(&_thread_func, kName, kPriority, this)),
	fPort(create_port(kPortCapacity, "DelayedMessageSender")),
	fSentCount(0)
{
	resume_thread(fThread);
}


/**
 * @brief Destroys the DelayedMessageSender, signaling the worker thread to exit.
 */
DelayedMessageSender::~DelayedMessageSender()
{
	// write the exit message to our port
	write_port(fPort, kExitMessage, NULL, 0);

	status_t status = B_OK;
	while (wait_for_thread(fThread, &status) == B_OK);

	// We now know the thread has exited, it is safe to cleanup
	delete_port(fPort);
}


/**
 * @brief Schedules @a message for delivery, merging with an existing pending
 *        message when possible.
 * @param message The DelayedMessage to schedule.
 * @return B_OK on success, B_NO_MEMORY or B_BAD_DATA on failure.
 */
status_t
DelayedMessageSender::ScheduleMessage(DelayedMessage& message)
{
	BAutolock _(fLock);

	// Can we merge with a pending message?
	ScheduledMessage* pending = NULL;
	for (int32 index = 0; index < fMessages.CountItems(); ++index) {
		pending = fMessages.ItemAt(index);
		if (pending->Merge(message))
			return B_OK;
	}

	// Guess not, add it to our list!
	ScheduledMessage* scheduled = new(std::nothrow) ScheduledMessage(message);

	if (scheduled == NULL)
		return B_NO_MEMORY;

	if (!scheduled->IsValid()) {
		delete scheduled;
		return B_BAD_DATA;
	}

	if (fMessages.AddItem(scheduled)) {
		fMessages.SortItems(&CompareMessages);
		_Wakeup(scheduled->ScheduledTime());
		return B_OK;
	}

	return B_ERROR;
}


/**
 * @brief Returns the number of messages currently waiting to be sent.
 * @return Count of pending ScheduledMessage objects.
 */
int32
DelayedMessageSender::CountDelayedMessages() const
{
	BAutolock _(fLock);
	return fMessages.CountItems();
}


/**
 * @brief Returns the total number of messages sent since construction.
 * @return Cumulative sent message count.
 */
int64
DelayedMessageSender::CountSentMessages() const
{
	return atomic_get64(&fSentCount);
}


/**
 * @brief Main loop for the sender thread; waits on a port and sends due messages.
 *
 * Reads from fPort with a timeout based on the next scheduled wakeup time.
 * On timeout, calls _SendDelayedMessages(). On kExitMessage, returns.
 */
void
DelayedMessageSender::_MessageLoop()
{
	int32 code = -1;
	status_t status = B_TIMED_OUT;
	bigtime_t timeout = B_INFINITE_TIMEOUT;

	while (true) {
		timeout = atomic_get64(&fScheduledWakeup);
		if (timeout != B_INFINITE_TIMEOUT)
			timeout -= (system_time() + (DM_MINIMUM_DELAY / 2));

		if (timeout > DM_MINIMUM_DELAY / 4) {
			status = read_port_etc(fPort, &code, NULL, 0, B_RELATIVE_TIMEOUT,
				timeout);
		} else
			status = B_TIMED_OUT;

		if (status == B_INTERRUPTED)
			continue;

		if (status == B_TIMED_OUT) {
			_SendDelayedMessages();
			continue;
		}

		if (status == B_OK) {
			switch (code) {
				case kWakeupMessage:
					continue;

				case kExitMessage:
					return;

				// TODO: trace unhandled messages
				default:
					continue;
			}
		}

		// port deleted?
		if (status < B_OK)
			break;
	}
}


/**
 * @brief Thread entry point for the sender thread.
 * @param sender Pointer to the owning DelayedMessageSender.
 * @return 0 always.
 */
int32
DelayedMessageSender::_thread_func(void* sender)
{
	(static_cast<DelayedMessageSender*>(sender))->_MessageLoop();
	return 0;
}


/**
 * @brief Sends all messages whose scheduled time has arrived.
 *
 * Must only be called from the sender thread. Acquires the lock with a short
 * timeout to avoid contention. Removes fully-delivered messages from the list
 * and updates the next wakeup time.
 *
 * @return Number of successful deliveries in this call.
 */
int32
DelayedMessageSender::_SendDelayedMessages()
{
	// avoid sending messages during times of contention
	if (fLock.LockWithTimeout(30000) != B_OK) {
		atomic_add64(&fScheduledWakeup, DM_MINIMUM_DELAY);
		return 0;
	}

	atomic_set64(&fScheduledWakeup, B_INFINITE_TIMEOUT);

	if (fMessages.CountItems() == 0) {
		fLock.Unlock();
		return 0;
	}

	int32 sent = 0;

	bigtime_t time = system_time() + DM_MINIMUM_DELAY / 2;
		// capture any that may be on the verge of being sent.

	BObjectList<ScheduledMessage> remove;

	ScheduledMessage* message = NULL;
	for (int32 index = 0; index < fMessages.CountItems(); ++index) {
		message = fMessages.ItemAt(index);

		if (message->ScheduledTime() > time) {
			atomic_set64(&fScheduledWakeup, message->ScheduledTime());
			break;
		}

		int32 sendCount = message->SendMessage();
		if (sendCount > 0)
			sent += sendCount;

		if (message->CountTargets() == 0)
			remove.AddItem(message);
	}

	// remove serviced messages
	for (int32 index = 0; index < remove.CountItems(); ++index)
		fMessages.RemoveItem(remove.ItemAt(index));

	atomic_add64(&fSentCount, sent);

	// catch any partly-failed messages (possibly late):
	if (fMessages.CountItems() > 0
		&& atomic_get64(&fScheduledWakeup) == B_INFINITE_TIMEOUT) {

		fMessages.SortItems(&CompareMessages);
		message = fMessages.ItemAt(0);
		bigtime_t timeout = message->ScheduledTime() - time;

		if (timeout < 0)
			timeout = DM_MINIMUM_DELAY;

		atomic_set64(&fScheduledWakeup, timeout);
	}

	fLock.Unlock();
	return sent;
}


/**
 * @brief Sends a wakeup notification to the sender thread if needed.
 * @param when The absolute system time at which a wakeup is required.
 */
void
DelayedMessageSender::_Wakeup(bigtime_t when)
{
	if (atomic_get64(&fScheduledWakeup) < when
		&& atomic_get(&fWakeupRetry) == 0)
		return;

	atomic_set64(&fScheduledWakeup, when);

	BPrivate::LinkSender sender(fPort);
	sender.StartMessage(kWakeupMessage);
	status_t error = sender.Flush(30000);
	atomic_set(&fWakeupRetry, (int32)error == B_TIMED_OUT);
}
