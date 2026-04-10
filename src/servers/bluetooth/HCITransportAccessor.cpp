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

/** @file HCITransportAccessor.cpp
 *  @brief HCI delegate that talks to the controller through a transport-driver descriptor. */


#include <String.h>

#include "BluetoothServer.h"
#include "HCITransportAccessor.h"


/**
 * @brief Construct a transport accessor and open the HCI driver device.
 *
 * Opens the transport-driver file at \a path for read/write access and
 * retrieves the HCI device ID assigned by the kernel via GET_HCI_ID ioctl.
 * If the device cannot be opened, the identifier is set to B_ERROR so
 * subsequent operations are rejected.
 *
 * @param path Filesystem path to the Bluetooth transport driver node
 *             (e.g. /dev/bluetooth/h2/0).
 */
HCITransportAccessor::HCITransportAccessor(BPath* path) : HCIDelegate(path)
{
	status_t status;

	fDescriptor = open (path->Path(), O_RDWR);
	if (fDescriptor > 0) {
		// find out which ID was assigned
		status = ioctl(fDescriptor, GET_HCI_ID, &fIdentifier, 0);
		printf("%s: hid retrieved %" B_PRIx32 " status=%" B_PRId32 "\n",
			__FUNCTION__, fIdentifier, status);
	} else {
		printf("%s: Device driver %s could not be opened %" B_PRId32 "\n",
			__FUNCTION__, path->Path(), fIdentifier);
		fIdentifier = B_ERROR;
	}

}


/**
 * @brief Destroy the transport accessor and close the driver descriptor.
 *
 * Closes the open file descriptor, if valid, and resets the identifier
 * to B_ERROR so it can no longer be used.
 */
HCITransportAccessor::~HCITransportAccessor()
{
	if (fDescriptor > 0) {
		close(fDescriptor);
		fDescriptor = -1;
		fIdentifier = B_ERROR;
	}
}


/**
 * @brief Submit a raw HCI command to the controller through the transport driver.
 *
 * Validates that both the HCI ID and file descriptor are valid, then
 * issues the command by calling ISSUE_BT_COMMAND ioctl on the driver.
 *
 * @param rc   Pointer to the raw HCI command buffer.
 * @param size Size in bytes of the command buffer.
 * @return The ioctl result on success, or B_ERROR if the ID or descriptor is
 *         invalid.
 */
status_t
HCITransportAccessor::IssueCommand(raw_command rc, size_t size)
{
	if (Id() < 0 || fDescriptor < 0)
		return B_ERROR;
/*
printf("### Command going: len = %ld\n", size);
for (uint16 index = 0 ; index < size; index++ ) {
	printf("%x:",((uint8*)rc)[index]);
}
printf("### \n");
*/

	return ioctl(fDescriptor, ISSUE_BT_COMMAND, rc, size);
}


/**
 * @brief Bring the Bluetooth transport up.
 *
 * Sends the BT_UP ioctl to the transport driver, telling it to power on
 * the radio and begin accepting HCI traffic.
 *
 * @return B_OK on success, or an error code from the ioctl.
 */
status_t
HCITransportAccessor::Launch() {

	uint32 dummy;
	return ioctl(fDescriptor, BT_UP, &dummy, sizeof(uint32));

}
