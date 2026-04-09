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
 *   Copyright 2007-2008, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file USBKit.h
 *  @brief USB device enumeration and data transfer classes (roster, device, configuration,
 *         interface, and endpoint). */

#ifndef _USBKIT_H
#define _USBKIT_H

#include <SupportDefs.h>
#include <USB_spec.h>
#include <USB_isochronous.h>


class BUSBRoster;
class BUSBDevice;
class BUSBConfiguration;
class BUSBInterface;
class BUSBEndpoint;


/** @brief Watches for USB devices being attached or removed from the bus.
 *
 *  Subclass this and implement the pure virtual DeviceAdded() and
 *  DeviceRemoved() hooks, then call Start() to begin watching. */
class BUSBRoster {
public:
									BUSBRoster();
virtual								~BUSBRoster();

	/** @brief Called when a new USB device is attached to the bus.
	 *
	 *  Return B_OK to retain the BUSBDevice object (DeviceRemoved() will later
	 *  be called for it). Any other return causes the object to be deleted
	 *  immediately and DeviceRemoved() will not be called.
	 *  @param device Newly attached, initialized device object.
	 *  @return B_OK to keep the device, or any error to release it. */
virtual	status_t					DeviceAdded(BUSBDevice *device) = 0;

	/** @brief Called when a device previously accepted by DeviceAdded() is detached.
	 *
	 *  The device object becomes invalid and will be deleted as soon as this
	 *  hook returns; remove all references to it before returning.
	 *  @param device The device being detached. */
virtual	void						DeviceRemoved(BUSBDevice *device) = 0;

	/** @brief Starts watching the USB bus for device changes. */
		void						Start();

	/** @brief Stops watching the USB bus. */
		void						Stop();

private:
virtual	void						_ReservedUSBRoster1();
virtual	void						_ReservedUSBRoster2();
virtual	void						_ReservedUSBRoster3();
virtual	void						_ReservedUSBRoster4();
virtual	void						_ReservedUSBRoster5();

		void *						fLooper;
		uint32						fReserved[10];
};


/** @brief Represents a USB device and provides access to its descriptors and configurations.
 *
 *  Can be obtained through BUSBRoster or constructed directly with a raw device path
 *  (e.g. "/dev/bus/usb/x"). Child configuration and interface objects are owned by
 *  this device and become invalid when the device is destroyed. */
class BUSBDevice {
public:
									BUSBDevice(const char *path = NULL);
virtual								~BUSBDevice();

	/** @brief Returns B_OK if the device was opened and initialized successfully.
	 *  @return B_OK if valid, or an error code. */
virtual	status_t					InitCheck();

	/** @brief Opens the device at the given raw path.
	 *  @param path Path to the raw USB device (e.g. "/dev/bus/usb/x").
	 *  @return B_OK on success, or an error code. */
		status_t					SetTo(const char *path);

	/** @brief Closes and resets the device, releasing all resources. */
		void						Unset();

	/** @brief Returns the bus location as a hub/device path string.
	 *  @return Null-terminated location string owned by this object. */
		const char *				Location() const;

	/** @brief Returns whether this device is a USB hub.
	 *  @return true if the device is a hub. */
		bool						IsHub() const;

	/** @brief Returns the USB specification version supported by the device.
	 *  @return BCD-encoded USB version (e.g. 0x0200 for USB 2.0). */
		uint16						USBVersion() const;

	/** @brief Returns the device class code from the device descriptor.
	 *  @return Class code byte. */
		uint8						Class() const;

	/** @brief Returns the device subclass code from the device descriptor.
	 *  @return Subclass code byte. */
		uint8						Subclass() const;

	/** @brief Returns the device protocol code from the device descriptor.
	 *  @return Protocol code byte. */
		uint8						Protocol() const;

	/** @brief Returns the maximum packet size for endpoint 0.
	 *  @return Maximum packet size in bytes. */
		uint8						MaxEndpoint0PacketSize() const;

	/** @brief Returns the vendor ID from the device descriptor.
	 *  @return Vendor ID. */
		uint16						VendorID() const;

	/** @brief Returns the product ID from the device descriptor.
	 *  @return Product ID. */
		uint16						ProductID() const;

	/** @brief Returns the device release version in BCD format.
	 *  @return BCD version number. */
		uint16						Version() const;

	/** @brief Returns the manufacturer string decoded to a C string.
	 *  @return Null-terminated string owned by this object; empty if unavailable. */
		const char *				ManufacturerString() const;

	/** @brief Returns the product string decoded to a C string.
	 *  @return Null-terminated string owned by this object; empty if unavailable. */
		const char *				ProductString() const;

	/** @brief Returns the serial number string decoded to a C string.
	 *  @return Null-terminated string owned by this object; empty if unavailable. */
		const char *				SerialNumberString() const;

	/** @brief Returns a pointer to the raw device descriptor.
	 *  @return Pointer to usb_device_descriptor owned by this object. */
		const usb_device_descriptor *
									Descriptor() const;

	/** @brief Reads a raw string descriptor by index.
	 *  @param index String descriptor index.
	 *  @param descriptor Buffer to receive the raw descriptor.
	 *  @param length Size of the buffer.
	 *  @return Number of bytes copied. */
		size_t						GetStringDescriptor(uint32 index,
										usb_string_descriptor *descriptor,
										size_t length) const;

	/** @brief Decodes a string descriptor to a new null-terminated C string.
	 *
	 *  The caller must delete the returned string with delete[].
	 *  @param index String descriptor index.
	 *  @return Newly allocated C string, or NULL on failure. */
		char *						DecodeStringDescriptor(uint32 index) const;

	/** @brief Retrieves a raw USB descriptor of the specified type.
	 *  @param type Descriptor type.
	 *  @param index Descriptor index.
	 *  @param languageID Language ID for string descriptors.
	 *  @param data Buffer to receive the descriptor.
	 *  @param length Size of the buffer.
	 *  @return Number of bytes copied. */
		size_t						GetDescriptor(uint8 type, uint8 index,
										uint16 languageID, void *data,
										size_t length) const;

	/** @brief Returns the number of configurations the device supports.
	 *  @return Configuration count. */
		uint32						CountConfigurations() const;

	/** @brief Returns the configuration object at the given index.
	 *  @param index Zero-based configuration index.
	 *  @return Pointer to BUSBConfiguration owned by this device, or NULL. */
		const BUSBConfiguration *	ConfigurationAt(uint32 index) const;

	/** @brief Returns the currently active configuration object.
	 *  @return Pointer to the active BUSBConfiguration, or NULL. */
		const BUSBConfiguration *	ActiveConfiguration() const;

	/** @brief Sets the active configuration.
	 *  @param configuration Configuration to activate (must be from this device).
	 *  @return B_OK on success, or an error code. */
		status_t					SetConfiguration(
										const BUSBConfiguration *configuration);

	/** @brief Sends a control request on the default pipe (endpoint 0).
	 *  @param requestType bmRequestType byte.
	 *  @param request bRequest byte.
	 *  @param value wValue field.
	 *  @param index wIndex field.
	 *  @param length wLength / data buffer size.
	 *  @param data Buffer for data phase (in or out).
	 *  @return Number of bytes transferred, or a negative error code. */
		ssize_t						ControlTransfer(uint8 requestType,
										uint8 request, uint16 value,
										uint16 index, uint16 length,
										void *data) const;

private:
virtual	void						_ReservedUSBDevice1();
virtual	void						_ReservedUSBDevice2();
virtual	void						_ReservedUSBDevice3();
virtual	void						_ReservedUSBDevice4();
virtual	void						_ReservedUSBDevice5();

		char *						fPath;
		int							fRawFD;

		usb_device_descriptor		fDescriptor;
		BUSBConfiguration **		fConfigurations;
		uint32						fActiveConfiguration;

mutable	char *						fManufacturerString;
mutable	char *						fProductString;
mutable	char *						fSerialNumberString;

		uint32						fReserved[10];
};


/** @brief Represents one configuration of a USB device.
 *
 *  Valid objects are obtained only through BUSBDevice::ConfigurationAt() or
 *  BUSBDevice::ActiveConfiguration(). Child interface objects are owned by
 *  this configuration and become invalid when it is deleted. */
class BUSBConfiguration {
public:
	/** @brief Returns the index of this configuration within its parent device.
	 *  @return Zero-based configuration index. */
		uint32						Index() const;

	/** @brief Returns the parent device of this configuration.
	 *  @return Pointer to the owning BUSBDevice. */
		const BUSBDevice *			Device() const;

	/** @brief Returns a descriptive string for this configuration.
	 *  @return Null-terminated string owned by this object; empty if unavailable. */
		const char *				ConfigurationString() const;

	/** @brief Returns a pointer to the raw configuration descriptor.
	 *  @return Pointer to usb_configuration_descriptor owned by this object. */
		const usb_configuration_descriptor *
									Descriptor() const;

	/** @brief Returns the number of interfaces in this configuration.
	 *  @return Interface count. */
		uint32						CountInterfaces() const;

	/** @brief Returns the interface object at the given index.
	 *  @param index Zero-based interface index.
	 *  @return Pointer to BUSBInterface owned by this configuration, or NULL. */
		const BUSBInterface *		InterfaceAt(uint32 index) const;

private:
friend	class BUSBDevice;
								BUSBConfiguration(BUSBDevice *device,
										uint32 index, int rawFD);
								~BUSBConfiguration();

		BUSBDevice *				fDevice;
		uint32						fIndex;
		int							fRawFD;

		usb_configuration_descriptor fDescriptor;
		BUSBInterface **			fInterfaces;

mutable	char *						fConfigurationString;

		uint32						fReserved[10];
};


/** @brief Represents a single interface within a USB configuration.
 *
 *  Provides access to descriptor fields, endpoints, and alternate interface settings.
 *  Valid objects are obtained only through BUSBConfiguration::InterfaceAt(). */
class BUSBInterface {
public:
	/** @brief Returns the interface index within its parent configuration.
	 *  @return Zero-based interface index. */
		uint32						Index() const;

	/** @brief Returns the alternate setting index currently active on this interface.
	 *  @return Alternate interface index. */
		uint32						AlternateIndex() const;

	/** @brief Returns the parent configuration of this interface.
	 *  @return Pointer to the owning BUSBConfiguration. */
		const BUSBConfiguration *	Configuration() const;

	/** @brief Returns the parent device of this interface.
	 *  @return Pointer to the parent BUSBDevice. */
		const BUSBDevice *			Device() const;

	/** @brief Returns the interface class code.
	 *  @return Class code byte. */
		uint8						Class() const;

	/** @brief Returns the interface subclass code.
	 *  @return Subclass code byte. */
		uint8						Subclass() const;

	/** @brief Returns the interface protocol code.
	 *  @return Protocol code byte. */
		uint8						Protocol() const;

	/** @brief Returns the descriptive string for this interface.
	 *  @return Null-terminated string owned by this object; empty if unavailable. */
		const char *				InterfaceString() const;

	/** @brief Returns a pointer to the raw interface descriptor.
	 *  @return Pointer to usb_interface_descriptor owned by this object. */
		const usb_interface_descriptor *
									Descriptor() const;

	/** @brief Retrieves a class- or vendor-specific descriptor by index.
	 *  @param index Zero-based descriptor index.
	 *  @param descriptor Buffer to receive the descriptor.
	 *  @param length Size of the buffer.
	 *  @return B_OK on success, or an error code. */
		status_t					OtherDescriptorAt(uint32 index,
										usb_descriptor *descriptor,
										size_t length) const;

	/** @brief Returns the number of endpoints in this interface alternate.
	 *  @return Endpoint count. */
		uint32						CountEndpoints() const;

	/** @brief Returns the endpoint object at the given index.
	 *  @param index Zero-based endpoint index.
	 *  @return Pointer to BUSBEndpoint owned by this interface, or NULL. */
		const BUSBEndpoint *		EndpointAt(uint32 index) const;

	/** @brief Returns the number of alternate settings for this interface.
	 *  @return Alternate count (includes the current alternate). */
		uint32						CountAlternates() const;

	/** @brief Returns the index of the currently active alternate setting.
	 *  @return Active alternate index. */
		uint32						ActiveAlternateIndex() const;

	/** @brief Returns the interface descriptor for an alternate setting without switching.
	 *  @param alternateIndex Zero-based alternate index.
	 *  @return Pointer to BUSBInterface for inspection only; do not use its endpoints. */
		const BUSBInterface *		AlternateAt(uint32 alternateIndex) const;

	/** @brief Switches this interface to the specified alternate setting.
	 *
	 *  All endpoint objects previously retrieved via EndpointAt() become invalid.
	 *  @param alternateIndex Zero-based alternate index to activate.
	 *  @return B_OK on success, or an error code. */
		status_t					SetAlternate(uint32 alternateIndex);

private:
friend	class BUSBConfiguration;
								BUSBInterface(BUSBConfiguration *config,
										uint32 index, uint32 alternate,
										int rawFD);
								~BUSBInterface();

		void						_UpdateDescriptorAndEndpoints();

		BUSBConfiguration *			fConfiguration;
		uint32						fIndex;
		uint32						fAlternate;
		int							fRawFD;

		usb_interface_descriptor	fDescriptor;
		BUSBEndpoint **				fEndpoints;

mutable	uint32						fAlternateCount;
mutable	BUSBInterface **			fAlternates;

mutable	char *						fInterfaceString;

		uint32						fReserved[10];
};


/** @brief Represents a USB endpoint and provides synchronous data transfer methods.
 *
 *  Valid objects are obtained only through BUSBInterface::EndpointAt(). */
class BUSBEndpoint {
public:
	/** @brief Returns the endpoint index within its parent interface.
	 *  @return Zero-based endpoint index. */
		uint32						Index() const;

	/** @brief Returns the parent interface of this endpoint.
	 *  @return Pointer to the owning BUSBInterface. */
		const BUSBInterface *		Interface() const;

	/** @brief Returns the parent configuration of this endpoint.
	 *  @return Pointer to the parent BUSBConfiguration. */
		const BUSBConfiguration *	Configuration() const;

	/** @brief Returns the parent device of this endpoint.
	 *  @return Pointer to the parent BUSBDevice. */
		const BUSBDevice *			Device() const;

	/** @brief Returns whether this is a bulk endpoint.
	 *  @return true if bulk transfer type. */
		bool						IsBulk() const;

	/** @brief Returns whether this is an interrupt endpoint.
	 *  @return true if interrupt transfer type. */
		bool						IsInterrupt() const;

	/** @brief Returns whether this is an isochronous endpoint.
	 *  @return true if isochronous transfer type. */
		bool						IsIsochronous() const;

	/** @brief Returns whether this is a control endpoint.
	 *  @return true if control transfer type. */
		bool						IsControl() const;

	/** @brief Returns whether data flows into the host on this endpoint.
	 *  @return true if direction is IN. */
		bool						IsInput() const;

	/** @brief Returns whether data flows out from the host on this endpoint.
	 *  @return true if direction is OUT. */
		bool						IsOutput() const;

	/** @brief Returns the maximum packet size of this endpoint.
	 *  @return Maximum packet size in bytes. */
		uint16						MaxPacketSize() const;

	/** @brief Returns the polling interval for interrupt or isochronous endpoints.
	 *  @return Interval in frames or microframes. */
		uint8						Interval() const;

	/** @brief Returns a pointer to the raw endpoint descriptor.
	 *  @return Pointer to usb_endpoint_descriptor owned by this object. */
		const usb_endpoint_descriptor *
									Descriptor() const;

	/** @brief Performs a synchronous control transfer on this endpoint.
	 *  @param requestType bmRequestType byte.
	 *  @param request bRequest byte.
	 *  @param value wValue field.
	 *  @param index wIndex field.
	 *  @param length wLength / data buffer size.
	 *  @param data Buffer for data phase.
	 *  @return Bytes transferred, or a negative error code. */
		ssize_t						ControlTransfer(uint8 requestType,
										uint8 request, uint16 value,
										uint16 index, uint16 length,
										void *data) const;

	/** @brief Performs a synchronous interrupt transfer.
	 *  @param data Buffer for the transfer.
	 *  @param length Size of the buffer.
	 *  @return Bytes transferred, or a negative error code. */
		ssize_t						InterruptTransfer(void *data,
										size_t length) const;

	/** @brief Performs a synchronous bulk transfer.
	 *  @param data Buffer for the transfer.
	 *  @param length Size of the buffer.
	 *  @return Bytes transferred, or a negative error code. */
		ssize_t						BulkTransfer(void *data,
										size_t length) const;

	/** @brief Performs a synchronous isochronous transfer.
	 *  @param data Buffer for the transfer.
	 *  @param length Total buffer size.
	 *  @param packetDescriptors Per-packet descriptor array.
	 *  @param packetCount Number of packets in the descriptor array.
	 *  @return Bytes transferred, or a negative error code. */
		ssize_t						IsochronousTransfer(void *data,
										size_t length,
										usb_iso_packet_descriptor *packetDescriptors,
										uint32 packetCount)	const;

	/** @brief Returns whether this endpoint is in the halted (stalled) state.
	 *  @return true if the endpoint is stalled. */
		bool						IsStalled() const;

	/** @brief Clears the halt condition on this endpoint.
	 *  @return B_OK on success, or an error code. */
		status_t					ClearStall() const;

private:
friend	class BUSBInterface;
								BUSBEndpoint(BUSBInterface *interface,
										uint32 index, int rawFD);
								~BUSBEndpoint();

		BUSBInterface *				fInterface;
		uint32						fIndex;
		int							fRawFD;

		usb_endpoint_descriptor		fDescriptor;

		uint32						fReserved[10];
};

#endif // _USB_KIT_H
