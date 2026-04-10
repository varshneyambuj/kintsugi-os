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
 *   Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file LocalDeviceHandler.h
 *  @brief Per-controller server-side handler that owns properties and pending request petitions. */

#ifndef _LOCALDEVICE_HANDLER_H_
#define _LOCALDEVICE_HANDLER_H_

#include <String.h>

#include <MessageQueue.h>

#include <bluetooth/bluetooth.h>

#include "HCIDelegate.h"

/** @brief Server-side base class representing one local Bluetooth controller.
 *
 * Holds the controller's HCI delegate, its cached properties, and the queue
 * of in-flight petitions (HCI events the daemon is currently waiting for).
 * LocalDeviceImpl extends this with the actual event-dispatch logic. */
class LocalDeviceHandler {

public:

	/** @brief Returns the HCI device id of the underlying delegate. */
	hci_id GetID();

	/** @brief Returns true if the controller is currently usable. */
	bool Available();
	/** @brief Marks the controller as in use by a client. */
	void Acquire(void);
	/** @brief Brings the controller up via the HCI delegate. */
	status_t Launch(void);

	/** @brief Returns the cached property message for this controller. */
	BMessage* 	  GetPropertiesMessage(void) { return fProperties; }
	/** @brief Returns true if @p property has been resolved on this controller. */
	bool  		  IsPropertyAvailable(const char* property);


protected:
	LocalDeviceHandler (HCIDelegate* hd);
	virtual ~LocalDeviceHandler();

	HCIDelegate*	fHCIDelegate;  /**< HCI command transport for this controller. */
	BMessage* 		fProperties;   /**< Cached controller properties (BD_ADDR, name, features, …). */

	/** @brief Records that the daemon is waiting for the events described by @p msg. */
	void 		AddWantedEvent(BMessage* msg);
	/** @brief Removes the (event, opcode) entry from the petition described by @p msg. */
	void 		ClearWantedEvent(BMessage* msg, uint16 event, uint16 opcode = 0);
	/** @brief Removes every entry of the petition described by @p msg. */
	void 		ClearWantedEvent(BMessage* msg);

	/** @brief Looks up the in-flight petition matching @p event / @p opcode.
	 *  @param indexFound Optional out parameter receiving the petition's queue index. */
	BMessage* 	FindPetition(uint16 event, uint16 opcode = 0, int32* indexFound = NULL);

private:

	BMessageQueue   fEventsWanted;  /**< Queue of in-flight HCI petitions awaiting a reply. */


};

#endif
