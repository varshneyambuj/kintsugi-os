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
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file HCITransportAccessor.h
 *  @brief HCI delegate that talks to the controller directly through a transport driver fd. */

#ifndef _HCITRANSPORT_ACCESSOR_H_
#define _HCITRANSPORT_ACCESSOR_H_

#include "HCIDelegate.h"


/** @brief HCI delegate using a raw transport driver file descriptor.
 *
 * Used when there is no kernel HCI socket layer in front of the controller —
 * the delegate opens the transport driver node directly and writes HCI
 * command packets to its file descriptor. */
class HCITransportAccessor : public HCIDelegate {

	public:
		/** @brief Opens the transport driver node at @p path. */
		HCITransportAccessor(BPath* path);
		~HCITransportAccessor();
		/** @brief Sends an HCI command directly through the transport descriptor. */
		status_t IssueCommand(raw_command rc, size_t size);
		/** @brief Issues the BT_UP ioctl and resolves the HCI device id. */
		status_t Launch();
	private:
		int fDescriptor;  /**< File descriptor of the open transport driver node. */
};

#endif
