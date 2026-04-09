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

/** @file DeviceClass.h
 *  @brief Bluetooth Class of Device (CoD) encapsulation with field accessors and rendering support. */

#ifndef _DEVICE_CLASS_H
#define _DEVICE_CLASS_H

#include <String.h>
#include <View.h>

namespace Bluetooth {

/** @brief Sentinel value representing an unknown or unset Class of Device. */
#define UNKNOWN_CLASS_OF_DEVICE 0x000000

/** @brief Encapsulates the Bluetooth Class of Device (CoD) 24-bit record, providing
 *         field-level accessors for service class, major device class, and minor device class,
 *         as well as string descriptions and icon rendering. */
class DeviceClass {

public:

	static const uint8 PixelsForIcon = 32; /**< Side length in pixels for device class icons. */
	static const uint8 IconInsets = 5;     /**< Inset in pixels applied when drawing device class icons. */

	/** @brief Constructs a DeviceClass from a raw 3-byte record array.
	 *  @param record Array of 3 bytes in little-endian order (byte[0] is LSB). */
	DeviceClass(uint8 record[3])
	{
		SetRecord(record);
	}


	/** @brief Constructs a DeviceClass from individual major, minor, and service class fields.
	 *  @param major   Major device class (5 bits, 0x00–0x1F).
	 *  @param minor   Minor device class (6 bits).
	 *  @param service Service class bitmask (11 bits). */
	DeviceClass(uint8 major, uint8 minor, uint16 service)
	{
		SetRecord(major, minor, service);
	}


	/** @brief Constructs an unknown/unset DeviceClass (record set to UNKNOWN_CLASS_OF_DEVICE). */
	DeviceClass(void)
	{
		fRecord = UNKNOWN_CLASS_OF_DEVICE;
	}

	/** @brief Sets the CoD record from a raw 3-byte array.
	 *  @param record Array of 3 bytes in little-endian order. */
	void SetRecord(uint8 record[3])
	{
		fRecord = record[0]|record[1]<<8|record[2]<<16;
	}

	/** @brief Sets the CoD record from individual field values.
	 *  @param major   Major device class (5 bits).
	 *  @param minor   Minor device class (6 bits).
	 *  @param service Service class bitmask (11 bits). */
	void SetRecord(uint8 major, uint8 minor, uint16 service)
	{
		fRecord = (minor & 0x3F) << 2;
		fRecord |= (major & 0x1F) << 8;
		fRecord |= (service & 0x7FF) << 13;
	}


	/** @brief Returns the 11-bit service class field from the CoD record.
	 *  @return Service class bitmask. */
	uint16 ServiceClass()
	{
		return (fRecord & 0x00FFE000) >> 13;
	}

	/** @brief Returns the 5-bit major device class field from the CoD record.
	 *  @return Major device class value. */
	uint8 MajorDeviceClass()
	{
		return (fRecord & 0x00001F00) >> 8;
	}

	/** @brief Returns the 6-bit minor device class field from the CoD record.
	 *  @return Minor device class value. */
	uint8 MinorDeviceClass()
	{
		return (fRecord & 0x000000FF) >> 2;
	}

	/** @brief Returns the full 24-bit raw CoD record.
	 *  @return The packed Class of Device value. */
	uint32 Record()
	{
		return fRecord;
	}

	/** @brief Checks whether the CoD record represents an unknown device class.
	 *  @return true if the record equals UNKNOWN_CLASS_OF_DEVICE, false otherwise. */
	bool IsUnknownDeviceClass()
	{
		return (fRecord == UNKNOWN_CLASS_OF_DEVICE);
	}

	/** @brief Appends a human-readable description of the service class to a string.
	 *  @param string BString to receive the service class description. */
	void GetServiceClass(BString&);

	/** @brief Appends a human-readable description of the major device class to a string.
	 *  @param string BString to receive the major device class description. */
	void GetMajorDeviceClass(BString&);

	/** @brief Appends a human-readable description of the minor device class to a string.
	 *  @param string BString to receive the minor device class description. */
	void GetMinorDeviceClass(BString&);

	/** @brief Appends a full human-readable dump of all CoD fields to a string.
	 *  @param string BString to receive the complete CoD description. */
	void DumpDeviceClass(BString&);

	/** @brief Draws the device class icon into a BView at the specified point.
	 *  @param view  The BView to draw into.
	 *  @param point The top-left origin for the icon. */
	void Draw(BView* view, const BPoint& point);

private:
	uint32 fRecord; /**< Packed 24-bit Class of Device record. */

};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::DeviceClass;
#endif

#endif // _DEVICE_CLASS_H
