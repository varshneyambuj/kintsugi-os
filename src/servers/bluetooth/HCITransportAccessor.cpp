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


HCITransportAccessor::~HCITransportAccessor()
{
	if (fDescriptor > 0) {
		close(fDescriptor);
		fDescriptor = -1;
		fIdentifier = B_ERROR;
	}
}


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


status_t
HCITransportAccessor::Launch() {

	uint32 dummy;
	return ioctl(fDescriptor, BT_UP, &dummy, sizeof(uint32));

}
