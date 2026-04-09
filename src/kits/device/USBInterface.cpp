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
 *       Salvatore Benedetto <salvatore.benedetto@gmail.com>
 */


/**
 * @file USBInterface.cpp
 * @brief USB interface descriptor wrapper and alternate setting support
 *
 * Implements BUSBInterface, which represents a single interface within a
 * USB configuration. Supports alternate settings and owns the BUSBEndpoint
 * objects belonging to the current active alternate. Instances are created
 * by BUSBConfiguration and should not be constructed directly.
 *
 * @see USBConfiguration.cpp, USBEndpoint.cpp
 */


#include <USBKit.h>
#include <usb_raw.h>

#include <new>
#include <string.h>
#include <unistd.h>


/**
 * @brief Constructs a BUSBInterface and retrieves its descriptor and endpoints.
 *
 * Calls _UpdateDescriptorAndEndpoints() to fetch the interface descriptor
 * (using the ETC variant that accepts an alternate index) and allocate
 * the matching BUSBEndpoint objects.
 *
 * @param config The owning BUSBConfiguration.
 * @param index Zero-based interface index within the configuration.
 * @param alternate The alternate setting index, or B_USB_RAW_ACTIVE_ALTERNATE
 *     to use whichever alternate is currently active on the device.
 * @param rawFD Open file descriptor to the usb_raw device node.
 */
BUSBInterface::BUSBInterface(BUSBConfiguration *config, uint32 index,
	uint32 alternate, int rawFD)
	:	fConfiguration(config),
		fIndex(index),
		fAlternate(alternate),
		fRawFD(rawFD),
		fEndpoints(NULL),
		fAlternateCount(0),
		fAlternates(NULL),
		fInterfaceString(NULL)
{
	_UpdateDescriptorAndEndpoints();
}


/**
 * @brief Destroys the BUSBInterface, releasing all endpoints and alternates.
 */
BUSBInterface::~BUSBInterface()
{
	delete[] fInterfaceString;

	if (fEndpoints != NULL) {
		for (int32 i = 0; i < fDescriptor.num_endpoints; i++)
			delete fEndpoints[i];
		delete[] fEndpoints;
	}

	if (fAlternates != NULL) {
		for (uint32 i = 0; i < fAlternateCount; i++)
			delete fAlternates[i];
		delete[] fAlternates;
	}
}


/**
 * @brief Returns the zero-based index of this interface within the configuration.
 * @return The interface index.
 */
uint32
BUSBInterface::Index() const
{
	return fIndex;
}


/**
 * @brief Returns the alternate setting index currently represented by this object.
 *
 * If this interface was constructed with B_USB_RAW_ACTIVE_ALTERNATE, queries
 * the device for the active index.
 *
 * @return The alternate setting index.
 */
uint32
BUSBInterface::AlternateIndex() const
{
	if (fAlternate == B_USB_RAW_ACTIVE_ALTERNATE)
		return ActiveAlternateIndex();
	return fAlternate;
}


/**
 * @brief Returns a pointer to the owning BUSBConfiguration.
 * @return The parent configuration.
 */
const BUSBConfiguration *
BUSBInterface::Configuration() const
{
	return fConfiguration;
}


/**
 * @brief Returns a pointer to the owning BUSBDevice.
 * @return The parent device.
 */
const BUSBDevice *
BUSBInterface::Device() const
{
	return fConfiguration->Device();
}


/**
 * @brief Returns the interface class code.
 * @return The USB interface class byte.
 */
uint8
BUSBInterface::Class() const
{
	return fDescriptor.interface_class;
}


/**
 * @brief Returns the interface subclass code.
 * @return The USB interface subclass byte.
 */
uint8
BUSBInterface::Subclass() const
{
	return fDescriptor.interface_subclass;
}


/**
 * @brief Returns the interface protocol code.
 * @return The USB interface protocol byte.
 */
uint8
BUSBInterface::Protocol() const
{
	return fDescriptor.interface_protocol;
}


/**
 * @brief Returns the human-readable string for this interface.
 *
 * Decodes the string descriptor on first access and caches the result.
 *
 * @return A null-terminated UTF-8 string, or an empty string if unavailable.
 */
const char *
BUSBInterface::InterfaceString() const
{
	if (fDescriptor.interface == 0)
		return "";

	if (fInterfaceString)
		return fInterfaceString;

	fInterfaceString = Device()->DecodeStringDescriptor(fDescriptor.interface);
	if (fInterfaceString == NULL)
		return "";

	return fInterfaceString;
}


/**
 * @brief Returns a pointer to the raw USB interface descriptor.
 * @return Pointer to the internal usb_interface_descriptor.
 */
const usb_interface_descriptor *
BUSBInterface::Descriptor() const
{
	return &fDescriptor;
}


/**
 * @brief Retrieves a non-standard (class- or vendor-specific) descriptor.
 *
 * Fetches generic descriptors beyond the standard interface descriptor using
 * B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR_ETC.
 *
 * @param index Zero-based index of the generic descriptor.
 * @param descriptor Buffer to receive the descriptor.
 * @param length Size of \a descriptor in bytes.
 * @return B_OK on success, B_BAD_VALUE if arguments are invalid, or B_ERROR
 *     if the ioctl fails.
 */
status_t
BUSBInterface::OtherDescriptorAt(uint32 index, usb_descriptor *descriptor,
	size_t length) const
{
	if (length <= 0 || descriptor == NULL)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.generic_etc.descriptor = descriptor;
	command.generic_etc.config_index = fConfiguration->Index();
	command.generic_etc.interface_index = fIndex;
	command.generic_etc.alternate_index = fAlternate;
	command.generic_etc.generic_index = index;
	command.generic_etc.length = length;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR_ETC, &command,
		sizeof(command)) || command.generic.status != B_USB_RAW_STATUS_SUCCESS)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Returns the number of endpoints in the current alternate setting.
 * @return The endpoint count from the interface descriptor.
 */
uint32
BUSBInterface::CountEndpoints() const
{
	return fDescriptor.num_endpoints;
}


/**
 * @brief Returns the endpoint at the specified index.
 * @param index Zero-based endpoint index.
 * @return Pointer to the BUSBEndpoint, or NULL if out of range.
 */
const BUSBEndpoint *
BUSBInterface::EndpointAt(uint32 index) const
{
	if (index >= fDescriptor.num_endpoints || fEndpoints == NULL)
		return NULL;

	return fEndpoints[index];
}


/**
 * @brief Returns the total number of alternate settings for this interface.
 *
 * Queries the device via B_USB_RAW_COMMAND_GET_ALT_INTERFACE_COUNT.
 *
 * @return The alternate count, or 1 if the query fails.
 */
uint32
BUSBInterface::CountAlternates() const
{
	usb_raw_command command;
	command.alternate.config_index = fConfiguration->Index();
	command.alternate.interface_index = fIndex;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_ALT_INTERFACE_COUNT, &command,
		sizeof(command)) || command.alternate.status != B_USB_RAW_STATUS_SUCCESS)
		return 1;

	return command.alternate.alternate_info;
}


/**
 * @brief Returns the BUSBInterface object for the specified alternate setting.
 *
 * Lazily allocates the full array of alternate BUSBInterface objects on first
 * access.
 *
 * @param alternateIndex Zero-based alternate setting index.
 * @return Pointer to the BUSBInterface for that alternate, or NULL if out of
 *     range or on allocation failure.
 */
const BUSBInterface *
BUSBInterface::AlternateAt(uint32 alternateIndex) const
{
	if (fAlternateCount > 0 && fAlternates != NULL) {
		if (alternateIndex >= fAlternateCount)
			return NULL;

		return fAlternates[alternateIndex];
	}

	if (fAlternateCount == 0)
		fAlternateCount = CountAlternates();
	if (alternateIndex >= fAlternateCount)
		return NULL;

	fAlternates = new(std::nothrow) BUSBInterface *[fAlternateCount];
	if (fAlternates == NULL)
		return NULL;

	for (uint32 i = 0; i < fAlternateCount; i++) {
		fAlternates[i] = new(std::nothrow) BUSBInterface(fConfiguration, fIndex,
			i, fRawFD);
	}

	return fAlternates[alternateIndex];
}


/**
 * @brief Returns the index of the currently active alternate setting.
 *
 * Queries the device via B_USB_RAW_COMMAND_GET_ACTIVE_ALT_INTERFACE_INDEX.
 *
 * @return The active alternate index, or 0 if the query fails.
 */
uint32
BUSBInterface::ActiveAlternateIndex() const
{
	usb_raw_command command;
	command.alternate.config_index = fConfiguration->Index();
	command.alternate.interface_index = fIndex;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_ACTIVE_ALT_INTERFACE_INDEX, &command,
		sizeof(command)) || command.alternate.status != B_USB_RAW_STATUS_SUCCESS)
		return 0;

	return command.alternate.alternate_info;
}


/**
 * @brief Switches the interface to the specified alternate setting.
 *
 * Sends B_USB_RAW_COMMAND_SET_ALT_INTERFACE and then refreshes the descriptor
 * and endpoint list to match the new alternate.
 *
 * @param alternateIndex Zero-based alternate setting index to activate.
 * @return B_OK on success, or B_ERROR if the ioctl fails.
 */
status_t
BUSBInterface::SetAlternate(uint32 alternateIndex)
{
	usb_raw_command command;
	command.alternate.alternate_info = alternateIndex;
	command.alternate.config_index = fConfiguration->Index();
	command.alternate.interface_index = fIndex;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_SET_ALT_INTERFACE, &command,
		sizeof(command)) || command.alternate.status != B_USB_RAW_STATUS_SUCCESS)
		return B_ERROR;

	_UpdateDescriptorAndEndpoints();
	return B_OK;
}


/**
 * @brief Refreshes the interface descriptor and rebuilds the endpoint list.
 *
 * Deletes any previously allocated BUSBEndpoint objects, fetches the current
 * interface descriptor via B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR_ETC,
 * and allocates a new BUSBEndpoint for each endpoint in the descriptor.
 */
void
BUSBInterface::_UpdateDescriptorAndEndpoints()
{
	if (fEndpoints != NULL) {
		// Delete old endpoints
		for (int32 i = 0; i < fDescriptor.num_endpoints; i++)
			delete fEndpoints[i];
		delete[] fEndpoints;
		fEndpoints = NULL;
	}

	usb_raw_command command;
	command.interface_etc.descriptor = &fDescriptor;
	command.interface_etc.config_index = fConfiguration->Index();
	command.interface_etc.interface_index = fIndex;
	command.interface_etc.alternate_index = fAlternate;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR_ETC, &command,
			sizeof(command)) || command.interface.status != B_USB_RAW_STATUS_SUCCESS)
		memset(&fDescriptor, 0, sizeof(fDescriptor));

	fEndpoints = new(std::nothrow) BUSBEndpoint *[fDescriptor.num_endpoints];
	if (fEndpoints == NULL)
		return;

	for (int32 i = 0; i < fDescriptor.num_endpoints; i++)
		fEndpoints[i] = new(std::nothrow) BUSBEndpoint(this, i, fRawFD);
}
