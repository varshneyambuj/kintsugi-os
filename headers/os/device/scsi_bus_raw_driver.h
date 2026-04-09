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
 * This file incorporates work from the Haiku project:
 *   Copyright 2002-2004, Thomas Kurschel. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file scsi_bus_raw_driver.h
 *  @brief ioctl codes for raw SCSI bus access via the devfs entry. */

#ifndef _SCSI_BUS_RAW_DRIVER_H
#define _SCSI_BUS_RAW_DRIVER_H


/*!	Part of Open SCSI bus manager

	Devfs entry for raw bus access.

	This interface will go away. It's used by scsi_probe as
	long as we have no proper pnpfs where all the info can
	be retrieved from.
*/


#include <Drivers.h>


/** @brief ioctl codes for raw SCSI bus operations.
 *
 *  Offsets start at B_DEVICE_OP_CODES_END + 300 to avoid collisions
 *  with other SCSI opcodes defined in scsiprobe_driver.h or scsi.h.
 *  All ioctl calls return the subsystem status (see SCSI.h). */
enum {
	B_SCSI_BUS_RAW_RESET = B_DEVICE_OP_CODES_END + 300,
	B_SCSI_BUS_RAW_PATH_INQUIRY
};

#endif	/* _SCSI_BUS_RAW_DRIVER_H */
