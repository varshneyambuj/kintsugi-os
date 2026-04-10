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

/** @file HCIControllerAccessor.cpp
 *  @brief HCI delegate that talks to the controller via the kernel HCI socket. */

#include "HCIControllerAccessor.h"

/**
 * @brief Construct an HCI controller accessor for the given device path.
 *
 * Initializes the base HCIDelegate with the supplied path.  No socket or
 * descriptor is opened at construction time.
 *
 * @param path Filesystem path to the HCI controller device node.
 */
HCIControllerAccessor::HCIControllerAccessor(BPath* path) : HCIDelegate(path)
{

}


/** @brief Destroy the HCI controller accessor. */
HCIControllerAccessor::~HCIControllerAccessor()
{

}


/**
 * @brief Submit a raw HCI command to the controller via the kernel socket.
 *
 * Currently a stub that validates the controller ID but does not actually
 * transmit data.
 *
 * @param rc   Pointer to the raw command buffer to send.
 * @param size Size in bytes of the command buffer.
 * @return B_OK if the controller ID is valid, B_ERROR otherwise.
 */
status_t
HCIControllerAccessor::IssueCommand(raw_command rc, size_t size)
{

	if (Id() < 0)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Activate the HCI controller.
 *
 * Currently a no-op stub that always succeeds.
 *
 * @return B_OK unconditionally.
 */
status_t
HCIControllerAccessor::Launch() {

	return B_OK;

}
