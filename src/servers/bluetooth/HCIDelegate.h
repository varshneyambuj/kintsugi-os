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
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file HCIDelegate.h
 *  @brief Abstract base for HCI command transports used by the Bluetooth server. */

#ifndef _HCIDELEGATE_H_
#define _HCIDELEGATE_H_

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <Path.h>

#include <bluetooth/HCI/btHCI_transport.h>

/** @brief Opaque handle to a serialised HCI command buffer. */
typedef void* raw_command;


/** @brief Abstract HCI delegate that knows how to send commands to one controller.
 *
 * Concrete subclasses (HCIControllerAccessor, HCITransportAccessor) implement
 * IssueCommand() and Launch() against either the kernel HCI socket or the
 * raw transport driver. Higher layers always go through QueueCommand() so
 * that future flow-control work can be added in one place. */
class HCIDelegate {

	public:
		/** @brief Constructs the delegate. The path argument is reserved for the future command queue. */
		HCIDelegate(BPath* path)
		{
			//TODO create such queue
			fIdentifier = -1;
		}


		/** @brief Returns the HCI device id this delegate talks to. */
		hci_id Id(void) const
		{
			return fIdentifier;
		}


		virtual ~HCIDelegate()
 		{

		}

		/** @brief Subclass hook: synchronously sends @p rc to the controller.
		 *  @param rc   Pointer to the raw HCI command buffer.
		 *  @param size Length of the command buffer in bytes.
		 *  @return B_OK on success, or an error code on failure. */
		virtual status_t IssueCommand(raw_command rc, size_t size)=0;
			// TODO means to be private use QueueCommand
		/** @brief Subclass hook: opens the underlying transport and resolves @c fIdentifier. */
		virtual status_t Launch()=0;


		/** @brief Releases @p slots HCI command credits back to the controller. */
		void FreeWindow(uint8 slots)
		{
			// TODO: hci control flow
		}


		/** @brief Queues an HCI command for transmission, applying any flow-control rules. */
		status_t QueueCommand(raw_command rc, size_t size)
		{
			// TODO: this is suposed to queue the command in a queue so all
			// are actually send to HW to implement HCI FlowControl requeriments
			return IssueCommand(rc, size);
		}

	protected:

		hci_id fIdentifier;  /**< HCI device id once Launch() succeeds, or -1. */

	private:

};

#endif
