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
 * @file USBDevice.cpp
 * @brief USB device wrapper for the Device Kit
 *
 * Implements BUSBDevice, which provides access to a USB device via the
 * usb_raw kernel driver. Handles device descriptor retrieval, configuration
 * management, string descriptor decoding (UCS-2 to UTF-8), and control
 * transfers.
 *
 * @see USBConfiguration.cpp, USBRoster.cpp
 */


#include <ByteOrder.h>
#include <USBKit.h>
#include <usb_raw.h>
#include <UTF8.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <new>


/**
 * @brief Constructs a BUSBDevice and optionally opens the device at \a path.
 * @param path Path to the usb_raw device node, or NULL to create an
 *     uninitialized object.
 */
BUSBDevice::BUSBDevice(const char *path)
	:	fPath(NULL),
		fRawFD(-1),
		fConfigurations(NULL),
		fActiveConfiguration(0),
		fManufacturerString(NULL),
		fProductString(NULL),
		fSerialNumberString(NULL)
{
	memset(&fDescriptor, 0, sizeof(fDescriptor));

	if (path)
		SetTo(path);
}


/**
 * @brief Destroys the BUSBDevice, closing the device and freeing all memory.
 */
BUSBDevice::~BUSBDevice()
{
	Unset();
}


/**
 * @brief Returns the initialization status of the device.
 * @return B_OK if the device is open and initialized, or B_ERROR otherwise.
 */
status_t
BUSBDevice::InitCheck()
{
	return (fRawFD >= 0 ? B_OK : B_ERROR);
}


/**
 * @brief Opens the device at \a path and retrieves its descriptor.
 *
 * Verifies the usb_raw protocol version, fetches the device descriptor, and
 * allocates one BUSBConfiguration per configuration listed in the descriptor.
 *
 * @param path Absolute path to the usb_raw device node.
 * @return B_OK on success, B_BAD_VALUE if \a path is NULL, or B_ERROR on
 *     driver communication failure.
 */
status_t
BUSBDevice::SetTo(const char *path)
{
	if (!path)
		return B_BAD_VALUE;

	fPath = strdup(path);
	fRawFD = open(path, O_RDWR | O_CLOEXEC);
	if (fRawFD < 0) {
		Unset();
		return B_ERROR;
	}

	usb_raw_command command;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_VERSION, &command, sizeof(command))
		|| command.version.status != B_USB_RAW_PROTOCOL_VERSION) {
		Unset();
		return B_ERROR;
	}

	command.device.descriptor = &fDescriptor;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_DEVICE_DESCRIPTOR, &command,
		sizeof(command)) || command.device.status != B_USB_RAW_STATUS_SUCCESS) {
		Unset();
		return B_ERROR;
	}

	fConfigurations = new(std::nothrow) BUSBConfiguration *[
		fDescriptor.num_configurations];
	if (fConfigurations == NULL)
		return B_NO_MEMORY;

	for (uint32 i = 0; i < fDescriptor.num_configurations; i++) {
		fConfigurations[i] = new(std::nothrow) BUSBConfiguration(this, i,
			fRawFD);
	}

	return B_OK;
}


/**
 * @brief Closes the device and releases all associated resources.
 *
 * Resets all internal state, including the file descriptor, path, cached
 * string descriptors, configuration objects, and the device descriptor.
 */
void
BUSBDevice::Unset()
{
	if (fRawFD >= 0)
		close(fRawFD);
	fRawFD = -1;

	free(fPath);
	fPath = NULL;

	delete[] fManufacturerString;
	delete[] fProductString;
	delete[] fSerialNumberString;
	fManufacturerString = fProductString = fSerialNumberString = NULL;

	if (fConfigurations != NULL) {
		for (int32 i = 0; i < fDescriptor.num_configurations; i++)
			delete fConfigurations[i];

		delete[] fConfigurations;
		fConfigurations = NULL;
	}

	memset(&fDescriptor, 0, sizeof(fDescriptor));
}


/**
 * @brief Returns the bus location string derived from the device path.
 *
 * Strips the leading "/dev/bus/usb" prefix (12 characters) from the path.
 *
 * @return Pointer into the path string at the location segment, or NULL if
 *     the path is too short or not set.
 */
const char *
BUSBDevice::Location() const
{
	if (!fPath || strlen(fPath) < 12)
		return NULL;

	return &fPath[12];
}


/**
 * @brief Returns whether this device is a USB hub.
 * @return \c true if the device class is 0x09 (Hub), \c false otherwise.
 */
bool
BUSBDevice::IsHub() const
{
	return fDescriptor.device_class == 0x09;
}


/**
 * @brief Returns the USB specification version supported by the device.
 * @return BCD-encoded USB version (e.g. 0x0200 for USB 2.0).
 */
uint16
BUSBDevice::USBVersion() const
{
	return fDescriptor.usb_version;
}


/**
 * @brief Returns the device class code.
 * @return The USB device class byte.
 */
uint8
BUSBDevice::Class() const
{
	return fDescriptor.device_class;
}


/**
 * @brief Returns the device subclass code.
 * @return The USB device subclass byte.
 */
uint8
BUSBDevice::Subclass() const
{
	return fDescriptor.device_subclass;
}


/**
 * @brief Returns the device protocol code.
 * @return The USB device protocol byte.
 */
uint8
BUSBDevice::Protocol() const
{
	return fDescriptor.device_protocol;
}


/**
 * @brief Returns the maximum packet size for endpoint 0.
 * @return Maximum packet size in bytes.
 */
uint8
BUSBDevice::MaxEndpoint0PacketSize() const
{
	return fDescriptor.max_packet_size_0;
}


/**
 * @brief Returns the USB vendor ID of the device.
 * @return 16-bit vendor ID.
 */
uint16
BUSBDevice::VendorID() const
{
	return fDescriptor.vendor_id;
}


/**
 * @brief Returns the USB product ID of the device.
 * @return 16-bit product ID.
 */
uint16
BUSBDevice::ProductID() const
{
	return fDescriptor.product_id;
}


/**
 * @brief Returns the device release number in BCD.
 * @return BCD-encoded device version.
 */
uint16
BUSBDevice::Version() const
{
	return fDescriptor.device_version;
}


/**
 * @brief Returns the manufacturer string for the device.
 *
 * Decodes the string descriptor on first access and caches the result.
 *
 * @return A null-terminated UTF-8 string, or an empty string if unavailable.
 */
const char *
BUSBDevice::ManufacturerString() const
{
	if (fDescriptor.manufacturer == 0)
		return "";

	if (fManufacturerString)
		return fManufacturerString;

	fManufacturerString = DecodeStringDescriptor(fDescriptor.manufacturer);
	if (fManufacturerString == NULL)
		return "";

	return fManufacturerString;
}


/**
 * @brief Returns the product string for the device.
 *
 * Decodes the string descriptor on first access and caches the result.
 *
 * @return A null-terminated UTF-8 string, or an empty string if unavailable.
 */
const char *
BUSBDevice::ProductString() const
{
	if (fDescriptor.product == 0)
		return "";

	if (fProductString)
		return fProductString;

	fProductString = DecodeStringDescriptor(fDescriptor.product);
	if (fProductString == NULL)
		return "";

	return fProductString;
}


/**
 * @brief Returns the serial number string for the device.
 *
 * Decodes the string descriptor on first access and caches the result.
 *
 * @return A null-terminated UTF-8 string, or an empty string if unavailable.
 */
const char *
BUSBDevice::SerialNumberString() const
{
	if (fDescriptor.serial_number == 0)
		return "";

	if (fSerialNumberString)
		return fSerialNumberString;

	fSerialNumberString = DecodeStringDescriptor(fDescriptor.serial_number);
	if (fSerialNumberString == NULL)
		return "";

	return fSerialNumberString;
}


/**
 * @brief Returns a pointer to the raw USB device descriptor.
 * @return Pointer to the internal usb_device_descriptor.
 */
const usb_device_descriptor *
BUSBDevice::Descriptor() const
{
	return &fDescriptor;
}


/**
 * @brief Retrieves a USB string descriptor by index.
 * @param index String descriptor index from the device.
 * @param descriptor Buffer to receive the raw string descriptor.
 * @param length Size of \a descriptor in bytes.
 * @return The number of bytes received, or 0 on failure.
 */
size_t
BUSBDevice::GetStringDescriptor(uint32 index,
	usb_string_descriptor *descriptor, size_t length) const
{
	if (!descriptor)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.string.descriptor = descriptor;
	command.string.string_index = index;
	command.string.length = length;

	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_STRING_DESCRIPTOR, &command,
		sizeof(command)) || command.string.status != B_USB_RAW_STATUS_SUCCESS)
		return 0;

	return command.string.length;
}


/**
 * @brief Decodes a USB string descriptor (UCS-2 LE) to a UTF-8 C string.
 *
 * Retrieves the raw string descriptor, byte-swaps the UCS-2 characters from
 * little-endian to big-endian (as required by convert_to_utf8()), then
 * converts to UTF-8. The caller owns the returned buffer.
 *
 * @param index String descriptor index.
 * @return A newly allocated null-terminated UTF-8 string, or NULL on failure.
 */
char *
BUSBDevice::DecodeStringDescriptor(uint32 index) const
{
	char buffer[300];
	usb_string_descriptor *stringDescriptor;
	stringDescriptor = (usb_string_descriptor *)&buffer;

	int32 stringLength = GetStringDescriptor(index, stringDescriptor,
		sizeof(buffer) - sizeof(usb_string_descriptor)) - 1;

	if (stringLength < 3)
		return NULL;

	int32 resultLength = 0;

	// USB is always little-endian, UCS-2 is big-endian.
	uint16* ustr = (uint16*)stringDescriptor->string;
	for (int32 i = 0; i < (stringLength / 2); i++) {
		// Increase size of result as needed by source character.
		const uint16 character = B_LENDIAN_TO_HOST_INT16(ustr[i]);
		resultLength++;
		if (character >= 0x80)
			resultLength++;
		if (character >= 0x800)
			resultLength++;

		ustr[i] = B_SWAP_INT16(ustr[i]);
	}

	char *result = new(std::nothrow) char[resultLength + 1];
	if (result == NULL)
		return NULL;

	status_t status = convert_to_utf8(B_UNICODE_CONVERSION,
		(const char*)stringDescriptor->string, &stringLength,
		result, &resultLength, NULL);
	if (status != B_OK) {
		delete[] result;
		return NULL;
	}
	result[resultLength] = 0;
	return result;
}


/**
 * @brief Retrieves an arbitrary USB descriptor by type, index, and language.
 * @param type Descriptor type constant.
 * @param index Descriptor index.
 * @param languageID Language ID for string descriptors (0 for others).
 * @param data Buffer to receive the descriptor data.
 * @param length Size of \a data in bytes.
 * @return The number of bytes received, or 0 on failure.
 */
size_t
BUSBDevice::GetDescriptor(uint8 type, uint8 index, uint16 languageID,
	void *data, size_t length) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.descriptor.type = type;
	command.descriptor.index = index;
	command.descriptor.language_id = languageID;
	command.descriptor.data = data;
	command.descriptor.length = length;

	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_DESCRIPTOR, &command,
		sizeof(command)) || command.descriptor.status != B_USB_RAW_STATUS_SUCCESS)
		return 0;

	return command.descriptor.length;
}


/**
 * @brief Returns the number of configurations available on the device.
 * @return The configuration count from the device descriptor.
 */
uint32
BUSBDevice::CountConfigurations() const
{
	return fDescriptor.num_configurations;
}


/**
 * @brief Returns the configuration at the specified index.
 * @param index Zero-based configuration index.
 * @return Pointer to the BUSBConfiguration, or NULL if out of range.
 */
const BUSBConfiguration *
BUSBDevice::ConfigurationAt(uint32 index) const
{
	if (index >= fDescriptor.num_configurations || fConfigurations == NULL)
		return NULL;

	return fConfigurations[index];
}


/**
 * @brief Returns the currently active configuration.
 * @return Pointer to the active BUSBConfiguration, or NULL if not set.
 */
const BUSBConfiguration *
BUSBDevice::ActiveConfiguration() const
{
	if (fConfigurations == NULL)
		return NULL;

	return fConfigurations[fActiveConfiguration];
}


/**
 * @brief Sets the active USB configuration on the device.
 * @param configuration Pointer to the desired BUSBConfiguration.
 * @return B_OK on success, B_BAD_VALUE if \a configuration is invalid, or
 *     B_ERROR if the ioctl fails.
 */
status_t
BUSBDevice::SetConfiguration(const BUSBConfiguration *configuration)
{
	if (!configuration || configuration->Index() >= fDescriptor.num_configurations)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.config.config_index = configuration->Index();

	if (ioctl(fRawFD, B_USB_RAW_COMMAND_SET_CONFIGURATION, &command,
		sizeof(command)) || command.config.status != B_USB_RAW_STATUS_SUCCESS)
		return B_ERROR;

	fActiveConfiguration = configuration->Index();
	return B_OK;
}


/**
 * @brief Performs a control transfer on the default pipe (endpoint 0).
 * @param requestType bmRequestType field (direction, type, recipient).
 * @param request bRequest field.
 * @param value wValue field.
 * @param index wIndex field.
 * @param length wLength field; number of bytes to transfer in the data stage.
 * @param data Buffer for the data stage; may be NULL if \a length is 0.
 * @return The number of bytes transferred on success, or a negative error code.
 */
ssize_t
BUSBDevice::ControlTransfer(uint8 requestType, uint8 request, uint16 value,
	uint16 index, uint16 length, void *data) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.control.request_type = requestType;
	command.control.request = request;
	command.control.value = value;
	command.control.index = index;
	command.control.length = length;
	command.control.data = data;

	if (ioctl(fRawFD, B_USB_RAW_COMMAND_CONTROL_TRANSFER, &command,
		sizeof(command)) || command.control.status != B_USB_RAW_STATUS_SUCCESS)
		return B_ERROR;

	return command.control.length;
}


//	#pragma mark - FBC protection


void BUSBDevice::_ReservedUSBDevice1() {};
void BUSBDevice::_ReservedUSBDevice2() {};
void BUSBDevice::_ReservedUSBDevice3() {};
void BUSBDevice::_ReservedUSBDevice4() {};
void BUSBDevice::_ReservedUSBDevice5() {};
