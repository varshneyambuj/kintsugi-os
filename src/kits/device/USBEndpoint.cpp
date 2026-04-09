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
 * @file USBEndpoint.cpp
 * @brief USB endpoint descriptor wrapper and transfer interface for the Device Kit
 *
 * Implements BUSBEndpoint, which represents a single USB endpoint within a
 * BUSBInterface. Provides transfer methods for control, interrupt, bulk, and
 * isochronous transfers, as well as stall detection and clearing.
 *
 * @see USBInterface.cpp, USBDevice.cpp
 */


#include <USBKit.h>
#include <usb_raw.h>
#include <unistd.h>
#include <string.h>


/**
 * @brief Constructs a BUSBEndpoint and retrieves its descriptor.
 *
 * Issues B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR_ETC to populate
 * \c fDescriptor using the endpoint's position within the active alternate
 * setting of its interface.
 *
 * @param interface The owning BUSBInterface.
 * @param index Zero-based endpoint index within the interface.
 * @param rawFD Open file descriptor to the usb_raw device node.
 */
BUSBEndpoint::BUSBEndpoint(BUSBInterface *interface, uint32 index, int rawFD)
	:	fInterface(interface),
		fIndex(index),
		fRawFD(rawFD)
{
	usb_raw_command command;
	command.endpoint_etc.descriptor = &fDescriptor;
	command.endpoint_etc.config_index = fInterface->Configuration()->Index();
	command.endpoint_etc.interface_index = fInterface->Index();
	command.endpoint_etc.alternate_index = fInterface->AlternateIndex();
	command.endpoint_etc.endpoint_index = fIndex;
	if (ioctl(fRawFD, B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR_ETC, &command,
		sizeof(command)) || command.endpoint_etc.status != B_USB_RAW_STATUS_SUCCESS)
		memset(&fDescriptor, 0, sizeof(fDescriptor));
}


/** @brief Destroys the BUSBEndpoint object. */
BUSBEndpoint::~BUSBEndpoint()
{
}


/**
 * @brief Returns the zero-based index of this endpoint within its interface.
 * @return The endpoint index.
 */
uint32
BUSBEndpoint::Index() const
{
	return fIndex;
}


/**
 * @brief Returns a pointer to the owning BUSBInterface.
 * @return The parent interface.
 */
const BUSBInterface *
BUSBEndpoint::Interface() const
{
	return fInterface;
}


/**
 * @brief Returns a pointer to the owning BUSBConfiguration.
 * @return The parent configuration.
 */
const BUSBConfiguration *
BUSBEndpoint::Configuration() const
{
	return fInterface->Configuration();
}


/**
 * @brief Returns a pointer to the owning BUSBDevice.
 * @return The parent device.
 */
const BUSBDevice *
BUSBEndpoint::Device() const
{
	return fInterface->Device();
}


/**
 * @brief Returns whether this endpoint is a Bulk endpoint.
 * @return \c true if the transfer type is USB_ENDPOINT_ATTR_BULK.
 */
bool
BUSBEndpoint::IsBulk() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_BULK;
}


/**
 * @brief Returns whether this endpoint is an Interrupt endpoint.
 * @return \c true if the transfer type is USB_ENDPOINT_ATTR_INTERRUPT.
 */
bool
BUSBEndpoint::IsInterrupt() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_INTERRUPT;
}


/**
 * @brief Returns whether this endpoint is an Isochronous endpoint.
 * @return \c true if the transfer type is USB_ENDPOINT_ATTR_ISOCHRONOUS.
 */
bool
BUSBEndpoint::IsIsochronous() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_ISOCHRONOUS;
}


/**
 * @brief Returns whether this endpoint is a Control endpoint.
 * @return \c true if the transfer type is USB_ENDPOINT_ATTR_CONTROL.
 */
bool
BUSBEndpoint::IsControl() const
{
	return (fDescriptor.attributes & USB_ENDPOINT_ATTR_MASK)
		== USB_ENDPOINT_ATTR_CONTROL;
}


/**
 * @brief Returns whether data flows into the host on this endpoint (IN).
 * @return \c true if the endpoint direction bit is set to IN.
 */
bool
BUSBEndpoint::IsInput() const
{
	return (fDescriptor.endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
		== USB_ENDPOINT_ADDR_DIR_IN;
}


/**
 * @brief Returns whether data flows out from the host on this endpoint (OUT).
 * @return \c true if the endpoint direction bit is set to OUT.
 */
bool
BUSBEndpoint::IsOutput() const
{
	return (fDescriptor.endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
		== USB_ENDPOINT_ADDR_DIR_OUT;
}


/**
 * @brief Returns the maximum packet size for this endpoint.
 * @return Maximum packet size in bytes.
 */
uint16
BUSBEndpoint::MaxPacketSize() const
{
	return fDescriptor.max_packet_size;
}


/**
 * @brief Returns the polling interval for interrupt or isochronous endpoints.
 * @return Interval value as specified in the endpoint descriptor.
 */
uint8
BUSBEndpoint::Interval() const
{
	return fDescriptor.interval;
}


/**
 * @brief Returns a pointer to the raw USB endpoint descriptor.
 * @return Pointer to the internal usb_endpoint_descriptor.
 */
const usb_endpoint_descriptor *
BUSBEndpoint::Descriptor() const
{
	return &fDescriptor;
}


/**
 * @brief Performs a control transfer on this endpoint.
 * @param requestType bmRequestType field.
 * @param request bRequest field.
 * @param value wValue field.
 * @param index wIndex field.
 * @param length wLength field; number of bytes in the data stage.
 * @param data Buffer for the data stage; may be NULL if \a length is 0.
 * @return The number of bytes transferred on success, or a negative error code.
 */
ssize_t
BUSBEndpoint::ControlTransfer(uint8 requestType, uint8 request, uint16 value,
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


/**
 * @brief Performs an interrupt transfer on this endpoint.
 * @param data Buffer for the transferred data.
 * @param length Number of bytes to transfer.
 * @return The number of bytes transferred on success, or a negative error code.
 */
ssize_t
BUSBEndpoint::InterruptTransfer(void *data, size_t length) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.transfer.interface = fInterface->Index();
	command.transfer.endpoint = fIndex;
	command.transfer.data = data;
	command.transfer.length = length;

	if (ioctl(fRawFD, B_USB_RAW_COMMAND_INTERRUPT_TRANSFER, &command,
		sizeof(command)) || command.transfer.status != B_USB_RAW_STATUS_SUCCESS)
		return B_ERROR;

	return command.transfer.length;
}


/**
 * @brief Performs a bulk transfer on this endpoint.
 * @param data Buffer for the transferred data.
 * @param length Number of bytes to transfer.
 * @return The number of bytes transferred on success, or a negative error code.
 */
ssize_t
BUSBEndpoint::BulkTransfer(void *data, size_t length) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.transfer.interface = fInterface->Index();
	command.transfer.endpoint = fIndex;
	command.transfer.data = data;
	command.transfer.length = length;

	if (ioctl(fRawFD, B_USB_RAW_COMMAND_BULK_TRANSFER, &command,
		sizeof(command)) || command.transfer.status != B_USB_RAW_STATUS_SUCCESS)
		return B_ERROR;

	return command.transfer.length;
}


/**
 * @brief Performs an isochronous transfer on this endpoint.
 * @param data Buffer for the transferred data.
 * @param length Total number of bytes to transfer across all packets.
 * @param packetDescriptors Array of per-packet descriptors (lengths, status).
 * @param packetCount Number of entries in \a packetDescriptors.
 * @return The number of bytes transferred on success, or a negative error code.
 */
ssize_t
BUSBEndpoint::IsochronousTransfer(void *data, size_t length,
	usb_iso_packet_descriptor *packetDescriptors, uint32 packetCount) const
{
	if (length > 0 && data == NULL)
		return B_BAD_VALUE;

	usb_raw_command command;
	command.isochronous.interface = fInterface->Index();
	command.isochronous.endpoint = fIndex;
	command.isochronous.data = data;
	command.isochronous.length = length;
	command.isochronous.packet_descriptors = packetDescriptors;
	command.isochronous.packet_count = packetCount;

	if (ioctl(fRawFD, B_USB_RAW_COMMAND_ISOCHRONOUS_TRANSFER, &command,
		sizeof(command)) || command.isochronous.status != B_USB_RAW_STATUS_SUCCESS)
		return B_ERROR;

	return command.isochronous.length;
}


/**
 * @brief Returns whether the endpoint's halt condition is set.
 *
 * Queries the endpoint status via a GET_STATUS control transfer and checks
 * the ENDPOINT_HALT feature bit.
 *
 * @return \c true if the endpoint is stalled, \c false otherwise.
 */
bool
BUSBEndpoint::IsStalled() const
{
	uint16 status = 0;
	Device()->ControlTransfer(USB_REQTYPE_ENDPOINT_IN,
		USB_REQUEST_GET_STATUS, USB_FEATURE_ENDPOINT_HALT,
		fDescriptor.endpoint_address, sizeof(status), &status);
	return status != 0;
}


/**
 * @brief Clears the halt condition on the endpoint.
 *
 * Sends a CLEAR_FEATURE request with ENDPOINT_HALT to reset the data toggle
 * and allow further transfers.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
BUSBEndpoint::ClearStall() const
{
	return Device()->ControlTransfer(USB_REQTYPE_ENDPOINT_OUT,
		USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
		fDescriptor.endpoint_address, 0, NULL);
}
