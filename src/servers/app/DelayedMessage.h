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
 * MIT License. Copyright 2015, Haiku.
 * Original author: Joseph Groover <looncraz@looncraz.net>.
 */

/** @file DelayedMessage.h
    @brief Stack-based helper for constructing and sending deferred, mergeable port messages. */

#ifndef AS_DELAYED_MESSAGE_H
#define AS_DELAYED_MESSAGE_H


#include <ObjectList.h>
#include <OS.h>


/** @brief Determines how a new DelayedMessage is merged with an existing message
           that shares the same code on the target port. */
enum DMMergeMode {
	DM_NO_MERGE			= 0, // Will send this message, and the other(s)
	DM_MERGE_REPLACE	= 1, // Replace older data with newer data
	DM_MERGE_CANCEL		= 2, // keeps older data, cancels this message
	DM_MERGE_DUPLICATES = 3  // If data is the same, cancel new message
};


/** @brief Bit flags selecting which data slots must match for a merge to apply. */
enum {
	DM_DATA_DEFAULT	= 0, // Match all for DUPLICATES & none for REPLACE modes.
	DM_DATA_1		= 1 << 0,
	DM_DATA_2		= 1 << 1,
	DM_DATA_3		= 1 << 2,
	DM_DATA_4		= 1 << 3,
	DM_DATA_5		= 1 << 4,
	DM_DATA_6		= 1 << 5
};


/** @brief Predefined delay constants in microseconds for common scheduling intervals. */
enum {
	DM_MINIMUM_DELAY		= 500ULL,
	DM_SHORT_DELAY			= 1000ULL,
	DM_120HZ_DELAY			= 8888ULL,
	DM_60HZ_DELAY			= 16666ULL,
	DM_MEDIUM_DELAY			= 15000ULL,
	DM_30HZ_DELAY			= 33332ULL,
	DM_15HZ_DELAY			= 66664ULL,
	DM_LONG_DELAY			= 100000ULL,
	DM_QUARTER_SECOND_DELAY	= 250000ULL,
	DM_HALF_SECOND_DELAY	= 500000ULL,
	DM_ONE_SECOND_DELAY		= 1000000ULL,
	DM_ONE_MINUTE_DELAY		= DM_ONE_SECOND_DELAY * 60,
	DM_ONE_HOUR_DELAY		= DM_ONE_MINUTE_DELAY * 60
};


class DelayedMessageData;


/** @brief Friendly API for creating messages to be sent at a future time.

    Messages can be sent with a relative delay, or at a set time. Messages with
    the same code can be merged according to various rules. Each message can
    have any number of target recipients.

    DelayedMessage is a throw-away object, it is to be created on the stack,
    Flush()'d, then left to be destructed when out of scope.
*/
class DelayedMessage {
	typedef void(*FailureCallback)(int32 code, port_id port, void* data);
public:
								/** @brief Constructs a DelayedMessage with a message code and delivery time.
								    @param code The message code to send.
								    @param delay Delivery delay in microseconds (or absolute time if isSpecificTime).
								    @param isSpecificTime If true, delay is an absolute system time. */
								DelayedMessage(int32 code, bigtime_t delay,
									bool isSpecificTime = false);

								~DelayedMessage();

			// At least one target port is required.
			/** @brief Adds a destination port to receive this message.
			    @param port The port_id to send to; at least one must be added.
			    @return true if the port was successfully added. */
			bool				AddTarget(port_id port);

			// Merge messages with the same code according to the following
			// rules and data matching mask.
			/** @brief Configures how this message merges with prior messages sharing the same code.
			    @param mode The merge strategy to apply.
			    @param match Bit mask of data slots that must match for merging. */
			void				SetMerge(DMMergeMode mode, uint32 match = 0);

			// Called for each port on which the message was failed to be sent.
			/** @brief Registers a callback invoked for each port on which delivery fails.
			    @param callback Function pointer called with the code, failing port, and user data.
			    @param data Arbitrary user data passed to the callback. */
			void				SetFailureCallback(FailureCallback callback,
									void* data = NULL);

			/** @brief Appends a typed value to the message payload.
			    @tparam Type The type of the value to attach.
			    @param data The value to copy into the message.
			    @return B_OK on success, or an error code. */
			template <class Type>
			status_t			Attach(const Type& data);

			/** @brief Appends raw bytes to the message payload.
			    @param data Pointer to the data to append.
			    @param size Number of bytes to append.
			    @return B_OK on success, or an error code. */
			status_t			Attach(const void* data, size_t size);

			/** @brief Appends all items in a BObjectList to the message payload.
			    @tparam Type Element type stored in the list.
			    @param list The list whose items are serialised into the payload.
			    @return B_OK on success, or an error code. */
			template <class Type>
			status_t			AttachList(const BObjectList<Type>& list);

			/** @brief Appends a filtered subset of a BObjectList to the payload.
			    @tparam Type Element type stored in the list.
			    @param list The list of items.
			    @param whichArray Boolean array; only items where whichArray[i] is true are appended.
			    @return B_OK on success, or an error code. */
			template <class Type>
			status_t			AttachList(const BObjectList<Type>& list,
									bool* whichArray);

			/** @brief Sends the message to all registered targets and relinquishes ownership.
			    @return B_OK if the message was dispatched successfully. */
			status_t			Flush();

		// Private
			/** @brief Transfers ownership of the internal data to the caller.
			    @return Pointer to the DelayedMessageData; caller is responsible for deletion. */
			DelayedMessageData*	HandOff();

			/** @brief Returns a non-owning pointer to the internal message data.
			    @return Pointer to the DelayedMessageData. */
			DelayedMessageData*	Data() {return fData;}

private:
		// Forbidden methods - these are one time-use objects.
			void*				operator new(size_t);
			void*				operator new[](size_t);

			DelayedMessageData*	fData;
			bool				fHandedOff;
};


// #pragma mark Implementation


template <class Type>
status_t
DelayedMessage::Attach(const Type& data)
{
	return Attach(&data, sizeof(Type));
}


template <class Type>
status_t
DelayedMessage::AttachList(const BObjectList<Type>& list)
{
	if (list.CountItems() == 0)
		return B_BAD_VALUE;

	status_t error = Attach<int32>(list.CountItems());

	for (int32 index = 0; index < list.CountItems(); ++index) {
		if (error != B_OK)
			break;

		error = Attach<Type>(*(list.ItemAt(index)));
	}

	return error;
}


template <class Type>
status_t
DelayedMessage::AttachList(const BObjectList<Type>& list, bool* which)
{
	if (list.CountItems() == 0)
		return B_BAD_VALUE;

	if (which == NULL)
		return AttachList(list);

	int32 count = 0;
	for (int32 index = 0; index < list.CountItems(); ++index) {
		if (which[index])
			++count;
	}

	if (count == 0)
		return B_BAD_VALUE;

	status_t error = Attach<int32>(count);

	for (int32 index = 0; index < list.CountItems(); ++index) {
		if (error != B_OK)
			break;

		if (which[index])
			error = Attach<Type>(*list.ItemAt(index));
	}

	return error;
}


#endif // AS_DELAYED_MESSAGE_H
