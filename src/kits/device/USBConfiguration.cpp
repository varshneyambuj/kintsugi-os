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
 *   Copyright 2007-2008, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file USBConfiguration.cpp
 * @brief USB configuration descriptor wrapper for the Device Kit
 *
 * Implements BUSBConfiguration, which wraps a USB configuration descriptor
 * and owns the set of BUSBInterface objects that belong to it. Instances
 * are created by BUSBDevice and should not be constructed directly.
 *
 * @see USBDevice.cpp, USBInterface.cpp
 */


#include <USBKit.h>
#include <usb_raw.h>
#include <unistd.h>
#include <string.h>
#include <new>


/**
 * @brief Constructs a BUSBConfiguration and retrieves its descriptor.
 *
 * Issues B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR to populate
 * \c fDescriptor, then allocates one BUSBInterface per interface reported
 * by the descriptor.
 *
 * @param device The owning BUSBDevice.
 * @param index Zero-based index of this configuration within the device.
 * @param rawFD Open file descriptor to the usb_raw device node.
 */
BUSBConfiguration::BUSBConfiguration(BUSBDevice *device, uint32 index, int rawFD)
	:	fDevice(device),
		fIndex(index),
		fRawFD(rawFD),
		fInterfaces(NULL),
		fConfigurationString(NULL)
{
	usb_raw_command command;
	command.config.descriptor = &fDescriptor;
	command.config.config_index = fIndex;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR, &command,
		sizeof(command)) || command.config.status != B_USB_RAW_STATUS_SUCCESS)
		memset(&fDescriptor, 0, sizeof(fDescriptor));

	fInterfaces = new(std::nothrow) BUSBInterface *[
		fDescriptor.number_interfaces];
	if (fInterfaces == NULL)
		return;

	for (uint32 i = 0; i < fDescriptor.number_interfaces; i++) {
		fInterfaces[i] = new(std::nothrow) BUSBInterface(this, i,
			B_USB_RAW_ACTIVE_ALTERNATE, fRawFD);
	}
}


/**
 * @brief Destroys the BUSBConfiguration, releasing all owned interfaces.
 */
BUSBConfiguration::~BUSBConfiguration()
{
	delete[] fConfigurationString;
	if (fInterfaces != NULL) {
		for (int32 i = 0; i < fDescriptor.number_interfaces; i++)
			delete fInterfaces[i];
		delete[] fInterfaces;
	}
}


/**
 * @brief Returns the zero-based index of this configuration within the device.
 * @return The configuration index.
 */
uint32
BUSBConfiguration::Index() const
{
	return fIndex;
}


/**
 * @brief Returns a pointer to the owning BUSBDevice.
 * @return The parent device.
 */
const BUSBDevice *
BUSBConfiguration::Device() const
{
	return fDevice;
}


/**
 * @brief Returns the human-readable string for this configuration.
 *
 * Decodes the string descriptor on first access and caches the result.
 *
 * @return A null-terminated UTF-8 string, or an empty string if unavailable.
 */
const char *
BUSBConfiguration::ConfigurationString() const
{
	if (fDescriptor.configuration == 0)
		return "";

	if (fConfigurationString)
		return fConfigurationString;

	fConfigurationString = Device()->DecodeStringDescriptor(
		fDescriptor.configuration);
	if (fConfigurationString == NULL)
		return "";

	return fConfigurationString;
}


/**
 * @brief Returns a pointer to the raw USB configuration descriptor.
 * @return Pointer to the internal usb_configuration_descriptor.
 */
const usb_configuration_descriptor *
BUSBConfiguration::Descriptor() const
{
	return &fDescriptor;
}


/**
 * @brief Returns the number of interfaces in this configuration.
 * @return The interface count as reported by the descriptor.
 */
uint32
BUSBConfiguration::CountInterfaces() const
{
	return fDescriptor.number_interfaces;
}


/**
 * @brief Returns the interface at the specified index.
 * @param index Zero-based interface index.
 * @return Pointer to the BUSBInterface, or NULL if \a index is out of range.
 */
const BUSBInterface *
BUSBConfiguration::InterfaceAt(uint32 index) const
{
	if (index >= fDescriptor.number_interfaces || fInterfaces == NULL)
		return NULL;

	return fInterfaces[index];
}
