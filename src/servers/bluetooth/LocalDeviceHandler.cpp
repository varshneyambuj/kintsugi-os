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
 *   Copyright 2007-2009 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file LocalDeviceHandler.cpp
 *  @brief Per-controller handler base — properties and in-flight HCI petition tracking. */

#include "LocalDeviceHandler.h"


/**
 * @brief Construct a local-device handler that wraps the given HCI delegate.
 *
 * Takes ownership of \a hd and allocates an empty BMessage to hold the
 * device's cached properties (version, features, address, etc.).
 *
 * @param hd Pointer to the HCIDelegate that provides low-level transport
 *           access for this controller.  Ownership is transferred.
 */
LocalDeviceHandler::LocalDeviceHandler(HCIDelegate* hd)
{
	fHCIDelegate = hd;
	fProperties = new BMessage();
}


/**
 * @brief Destroy the handler, freeing the HCI delegate and properties message.
 */
LocalDeviceHandler::~LocalDeviceHandler()
{
	delete fHCIDelegate;
	delete fProperties;
}


/** @brief Return the HCI device ID assigned by the kernel to this controller. */
hci_id
LocalDeviceHandler::GetID()
{
	return fHCIDelegate->Id();
}


/**
 * @brief Activate the underlying HCI transport.
 *
 * Delegates to HCIDelegate::Launch() to bring the radio up.
 *
 * @return B_OK on success, or an error from the delegate.
 */
status_t
LocalDeviceHandler::Launch(void)
{
	return fHCIDelegate->Launch();
}


/**
 * @brief Check whether this local device is available for acquisition.
 *
 * @return Currently always returns true.
 */
bool
LocalDeviceHandler::Available()
{

	return true;
}


/**
 * @brief Mark this device as acquired by a client.
 *
 * Currently a no-op placeholder; future implementations should track
 * ownership so the device is not handed out to multiple clients.
 */
void
LocalDeviceHandler::Acquire(void)
{

}


/**
 * @brief Test whether a named property has already been cached.
 *
 * Looks for \a property in the internal fProperties BMessage.
 *
 * @param property The property name to look up (e.g. "hci_version").
 * @return true if the property exists, false otherwise.
 */
bool
LocalDeviceHandler::IsPropertyAvailable(const char* property)
{
	type_code typeFound;
	int32 countFound;

	return (fProperties->GetInfo(property, &typeFound, &countFound) == B_OK );
}


/**
 * @brief Enqueue an HCI petition (expected-event request) for later matching.
 *
 * Thread-safe: acquires fEventsWanted lock before inserting.
 *
 * @param msg The BMessage petition describing the event(s) and opcode(s) to
 *            wait for.  Ownership is transferred to the queue.
 */
void
LocalDeviceHandler::AddWantedEvent(BMessage* msg)
{
	fEventsWanted.Lock();
	// TODO: review why it is needed to replicate the msg
//	printf("Adding request... %p\n", msg);
	fEventsWanted.AddMessage(msg);
	fEventsWanted.Unlock();
}


/**
 * @brief Remove an entire petition from the wanted-events queue.
 *
 * Thread-safe: acquires fEventsWanted lock before removing.
 *
 * @param msg The petition message to remove.
 */
void
LocalDeviceHandler::ClearWantedEvent(BMessage* msg)
{
	fEventsWanted.Lock();
	fEventsWanted.RemoveMessage(msg);
	fEventsWanted.Unlock();
}


/**
 * @brief Remove a specific event/opcode entry from a petition message.
 *
 * Scans the "eventExpected" array inside \a msg for an entry that matches
 * \a event (and optionally \a opcode).  When found, only that array entry
 * is removed; the petition itself stays in the queue if it still contains
 * other expected events.
 *
 * Thread-safe: acquires fEventsWanted lock for the duration of the scan.
 *
 * @param msg    The petition whose event entries should be pruned.
 * @param event  The HCI event code to remove.
 * @param opcode The HCI command opcode that must also match, or 0 to match
 *               by event code alone.
 */
void
LocalDeviceHandler::ClearWantedEvent(BMessage* msg, uint16 event, uint16 opcode)
{
	// Remove the whole petition from queue
	fEventsWanted.Lock();

	int16 eventFound;
	int16 opcodeFound;
	int32 eventIndex = 0;

	// for each Event
	while (msg->FindInt16("eventExpected", eventIndex, &eventFound) == B_OK) {

		printf("%s:Event expected %d@%" B_PRId32 "...\n", __FUNCTION__, event,
			eventIndex);

		if (eventFound == event) {

			printf("%s:Event matches@%" B_PRId32 "\n", __FUNCTION__, eventIndex);
			// there is an opcode specified
			if (opcode != 0) {

				// The opcode matches
				if ((msg->FindInt16("opcodeExpected", eventIndex, &opcodeFound) == B_OK)
					&& ((uint16)opcodeFound == opcode)) {

					// this should remove only the entry
					printf("Removed event %#x and opcode %d from request %p\n",
						event, opcode, msg);
					(void)msg->RemoveData("eventExpected", eventIndex);
					(void)msg->RemoveData("opcodeExpected", eventIndex);
					goto finish;
				}

			} else {
				// Event matches so far
				printf("Removed event %d from message %p\n", event, msg);
				(void)msg->RemoveData("eventExpected", eventIndex);
				goto finish;
			}

		}
		eventIndex++;
	}
	printf("%s:Nothing Found/Removed\n", __FUNCTION__);

finish:
	fEventsWanted.Unlock();

}


/**
 * @brief Search the petition queue for one that expects the given event and opcode.
 *
 * Iterates all queued petitions and their "eventExpected" / "opcodeExpected"
 * arrays to find a match.  If \a opcode is 0 (the default), the search
 * matches on event code alone.
 *
 * Thread-safe: acquires and releases fEventsWanted lock internally.
 *
 * @param event      The HCI event code to look for.
 * @param opcode     The HCI command opcode to match, or 0 for event-only
 *                   matching.
 * @param indexFound If non-NULL, receives the array index within the matched
 *                   petition where the event was found.
 * @return The matching BMessage petition, or NULL if none was found.
 */
BMessage*
LocalDeviceHandler::FindPetition(uint16 event, uint16 opcode, int32* indexFound)
{
	//debug data
	int16 eventFound;
	int16 opcodeFound;
	int32 eventIndex;

	fEventsWanted.Lock();
	// for each Petition
	for (int32 index = 0 ; index < fEventsWanted.CountMessages() ; index++) {
		BMessage* msg = fEventsWanted.FindMessage(index);
//		printf("%s:Petition %ld ... of %ld msg #%p\n", __FUNCTION__, index,
//			fEventsWanted.CountMessages(), msg);
//		msg->PrintToStream();
		eventIndex = 0;

		// for each Event
		while (msg->FindInt16("eventExpected", eventIndex, &eventFound) == B_OK ) {
			if (eventFound == event) {

//				printf("%s:Event %d found@%ld...", __FUNCTION__, event, eventIndex);
				// there is an opcode specified..
				if (msg->FindInt16("opcodeExpected", eventIndex, &opcodeFound)
					== B_OK) {
					// ensure the opcode
					if ((uint16)opcodeFound != opcode) {
//						printf("%s:opcode does not match %d\n",
//							__FUNCTION__, opcode);
						eventIndex++;
						continue;
					}
//					printf("Opcode matches %d\n", opcode);
				} else {
//					printf("No opcode specified\n");
				}

				fEventsWanted.Unlock();
				if (indexFound != NULL)
					*indexFound = eventIndex;
				return msg;
			}
			eventIndex++;
		}
	}
//	printf("%s:Event %d not found\n", __FUNCTION__, event);

	fEventsWanted.Unlock();
	return NULL;

}
