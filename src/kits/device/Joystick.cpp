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
 *   Copyright 2002-2008, Marcus Overhagen, Stefano Ceccherini, Fredrik Modéen.
 *   All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Joystick.cpp
 * @brief Joystick device interface for the Device Kit
 *
 * Implements the BJoystick class, which provides access to joystick and
 * gamepad devices. Supports both legacy standard mode (two axes, two buttons)
 * and enhanced mode (arbitrary axes, hats, and buttons via variable-size
 * reads). Device enumeration is performed via _BJoystickTweaker.
 *
 * @see JoystickTweaker.cpp, SerialPort.cpp
 */


#include <Joystick.h>
#include <JoystickTweaker.h>

#include <new>
#include <stdio.h>
#include <sys/ioctl.h>

#include <Debug.h>
#include <Directory.h>
#include <List.h>
#include <Path.h>
#include <String.h>


#if DEBUG
static FILE *sLogFile = NULL;

inline void
LOG(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	fputs(buf, sLogFile); fflush(sLogFile);
}

#	define LOG_ERR(text...) LOG(text)

#else
#	define LOG(text...)
#	define LOG_ERR(text...) fprintf(stderr, text)
#endif

#define CALLED() LOG("%s\n", __PRETTY_FUNCTION__)


/**
 * @brief Constructs a BJoystick object and scans for available devices.
 *
 * Initializes legacy member variables (timestamp, horizontal, vertical,
 * button1, button2) to safe defaults and allocates internal structures for
 * joystick info and per-stick data. Calls RescanDevices() to populate the
 * device list.
 */
BJoystick::BJoystick()
	:
	// legacy members for standard mode
	timestamp(0),
	horizontal(0),
	vertical(0),
	button1(true),
	button2(true),

	fBeBoxMode(false),
	fFD(-1),
	fDevices(new(std::nothrow) BList),
	fJoystickInfo(new(std::nothrow) joystick_info),
	fJoystickData(new(std::nothrow) BList)
{
#if DEBUG
	sLogFile = fopen("/var/log/joystick.log", "a");
#endif

	if (fJoystickInfo != NULL) {
		memset(&fJoystickInfo->module_info, 0, sizeof(joystick_module_info));
		fJoystickInfo->calibration_enable = false;
		fJoystickInfo->max_latency = 0;
	}

	RescanDevices();
}


/**
 * @brief Destroys the BJoystick object, closing the device and freeing memory.
 *
 * Closes the file descriptor if open, and releases all allocated per-stick
 * variable_joystick structures and device name strings.
 */
BJoystick::~BJoystick()
{
	if (fFD >= 0)
		close(fFD);

	if (fDevices != NULL) {
		for (int32 i = 0; i < fDevices->CountItems(); i++)
			delete (BString *)fDevices->ItemAt(i);

		delete fDevices;
	}

	delete fJoystickInfo;

	if (fJoystickData != NULL) {
		for (int32 i = 0; i < fJoystickData->CountItems(); i++) {
			variable_joystick *variableJoystick
				= (variable_joystick *)fJoystickData->ItemAt(i);
			if (variableJoystick == NULL)
				continue;

			free(variableJoystick->data);
			delete variableJoystick;
		}

		delete fJoystickData;
	}
}


/**
 * @brief Opens a joystick port in enhanced mode.
 * @param portName The name of the port (e.g. "joystick/isa_joy/1" or an
 *     absolute path).
 * @return A positive file descriptor on success, or a negative error code.
 * @see Open(const char*, bool)
 */
status_t
BJoystick::Open(const char *portName)
{
	CALLED();
	return Open(portName, true);
}


/**
 * @brief Opens a joystick port, optionally selecting enhanced mode.
 *
 * Resolves the port name to an absolute path under DEVICE_BASE_PATH if it
 * is not already absolute. Reads the joystick description file for the port
 * via _BJoystickTweaker, then allocates one variable_joystick per stick
 * reported by the driver.
 *
 * @param portName The name of the port.
 * @param enhanced If \c true, requests enhanced (variable-size read) mode.
 * @return A positive file descriptor on success, or a negative error code.
 */
status_t
BJoystick::Open(const char *portName, bool enhanced)
{
	CALLED();

	if (portName == NULL)
		return B_BAD_VALUE;

	if (fJoystickInfo == NULL || fJoystickData == NULL)
		return B_NO_INIT;

	fBeBoxMode = !enhanced;

	char nameBuffer[64];
	if (portName[0] != '/') {
		snprintf(nameBuffer, sizeof(nameBuffer), DEVICE_BASE_PATH"/%s",
			portName);
	} else
		snprintf(nameBuffer, sizeof(nameBuffer), "%s", portName);

	if (fFD >= 0)
		close(fFD);

	// TODO: BeOS don't use O_EXCL, and this seems to lead to some issues. I
	// added this flag having read some comments by Marco Nelissen on the
	// annotated BeBook. I think BeOS uses O_RDWR | O_NONBLOCK here.
	fFD = open(nameBuffer, O_RDWR | O_NONBLOCK | O_EXCL);
	if (fFD < 0)
		return B_ERROR;

	// read the Joystick Description file for this port/joystick
	_BJoystickTweaker joystickTweaker(*this);
	joystickTweaker.GetInfo(fJoystickInfo, portName);

	// signal that we support variable reads
	fJoystickInfo->module_info.flags |= js_flag_variable_size_reads;

	LOG("ioctl - %d\n", fJoystickInfo->module_info.num_buttons);
	ioctl(fFD, B_JOYSTICK_SET_DEVICE_MODULE, &fJoystickInfo->module_info,
		sizeof(joystick_module_info));
	ioctl(fFD, B_JOYSTICK_GET_DEVICE_MODULE, &fJoystickInfo->module_info,
		sizeof(joystick_module_info));
	LOG("ioctl - %d\n", fJoystickInfo->module_info.num_buttons);

	// Allocate the variable_joystick structures to hold the info for each
	// "stick". Note that the whole num_sticks thing seems a bit bogus, as
	// all sticks would be required to have exactly the same attributes,
	// i.e. axis, hat and button counts, since there is only one global
	// joystick_info for the whole device. What's implemented here is a
	// "best guess", using the read position in Update() to select the
	// stick for which data shall be returned.
	bool supportsVariable
		= (fJoystickInfo->module_info.flags & js_flag_variable_size_reads) != 0;
	for (uint16 i = 0; i < fJoystickInfo->module_info.num_sticks; i++) {
		variable_joystick *variableJoystick
			= new(std::nothrow) variable_joystick;
		if (variableJoystick == NULL)
			return B_NO_MEMORY;

		status_t result;
		if (supportsVariable) {
			// The driver supports arbitrary controls.
			result = variableJoystick->initialize(
				fJoystickInfo->module_info.num_axes,
				fJoystickInfo->module_info.num_hats,
				fJoystickInfo->module_info.num_buttons);
		} else {
			// The driver doesn't support our variable requests so we construct
			// a data structure that is compatible with extended_joystick and
			// just use that in reads. This allows us to use a single data
			// format internally but be compatible with both inputs.
			result = variableJoystick->initialize_to_extended_joystick();

			// Also ensure that we don't read over those boundaries.
			if (fJoystickInfo->module_info.num_axes > MAX_AXES)
				fJoystickInfo->module_info.num_axes = MAX_AXES;
			if (fJoystickInfo->module_info.num_hats > MAX_HATS)
				fJoystickInfo->module_info.num_hats = MAX_HATS;
			if (fJoystickInfo->module_info.num_buttons > MAX_BUTTONS)
				fJoystickInfo->module_info.num_buttons = MAX_BUTTONS;
		}

		if (result != B_OK) {
			delete variableJoystick;
			return result;
		}

		if (!fJoystickData->AddItem(variableJoystick)) {
			free(variableJoystick->data);
			delete variableJoystick;
			return B_NO_MEMORY;
		}
	}

	return fFD;
}


/** @brief Closes the joystick port and resets the file descriptor. */
void
BJoystick::Close(void)
{
	CALLED();
	if (fFD >= 0) {
		close(fFD);
		fFD = -1;
	}
}


/**
 * @brief Reads the current state of all sticks from the joystick driver.
 *
 * Populates per-stick variable_joystick buffers via read_pos(). Also updates
 * the legacy timestamp, horizontal, vertical, button1, and button2 members
 * from the first stick's data.
 *
 * @return B_OK on success, or an error code if the device is not open or
 *     a read fails.
 */
status_t
BJoystick::Update()
{
	CALLED();
	if (fJoystickInfo == NULL || fJoystickData == NULL || fFD < 0)
		return B_NO_INIT;

	for (uint16 i = 0; i < fJoystickInfo->module_info.num_sticks; i++) {
		variable_joystick *values
			= (variable_joystick *)fJoystickData->ItemAt(i);
		if (values == NULL)
			return B_NO_INIT;

		ssize_t result = read_pos(fFD, i, values->data,
			values->data_size);
		if (result < 0)
			return result;

		if ((size_t)result != values->data_size)
			return B_ERROR;

		if (i > 0)
			continue;

		// fill in the legacy values for the first stick
		timestamp = *values->timestamp;

		if (values->axis_count >= 1)
			horizontal = values->axes[0];
		else
			horizontal = 0;

		if (values->axis_count >= 2)
			vertical = values->axes[1];
		else
			vertical = 0;

		if (values->button_blocks > 0) {
			button1 = (*values->buttons & 1) == 0;
			button2 = (*values->buttons & 2) == 0;
		} else {
			button1 = true;
			button2 = true;
		}
	}

	return B_OK;
}


/**
 * @brief Sets the maximum input latency for the joystick driver.
 * @param maxLatency The desired maximum latency in microseconds.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BJoystick::SetMaxLatency(bigtime_t maxLatency)
{
	CALLED();
	if (fJoystickInfo == NULL || fFD < 0)
		return B_NO_INIT;

	status_t result = ioctl(fFD, B_JOYSTICK_SET_MAX_LATENCY, &maxLatency,
		sizeof(maxLatency));
	if (result == B_OK)
		fJoystickInfo->max_latency = maxLatency;

	return result;
}


/**
 * @brief Returns the number of joystick devices currently known to the system.
 * @return The count of entries in the device list.
 */
int32
BJoystick::CountDevices()
{
	CALLED();

	if (fDevices == NULL)
		return 0;

	int32 count = fDevices->CountItems();

	LOG("Count = %d\n", count);
	return count;
}


/**
 * @brief Retrieves the name of the device at the given index.
 * @param index Zero-based index into the device list.
 * @param name Buffer to receive the device name string.
 * @param bufSize Size of \a name in bytes.
 * @return B_OK on success, B_BAD_INDEX if \a index is out of range,
 *     B_NAME_TOO_LONG if the name does not fit, or B_BAD_VALUE / B_NO_INIT
 *     on other errors.
 */
status_t
BJoystick::GetDeviceName(int32 index, char *name, size_t bufSize)
{
	CALLED();
	if (fDevices == NULL)
		return B_NO_INIT;

	if (index >= fDevices->CountItems())
		return B_BAD_INDEX;

	if (name == NULL)
		return B_BAD_VALUE;

	BString *deviceName = (BString *)fDevices->ItemAt(index);
	if (deviceName->Length() > (int32)bufSize)
		return B_NAME_TOO_LONG;

	strlcpy(name, deviceName->String(), bufSize);
	LOG("Device Name = %s\n", name);
	return B_OK;
}


/**
 * @brief Rescans the system for available joystick devices.
 *
 * Clears and rebuilds the internal device list by scanning all joystick
 * paths including disabled devices.
 *
 * @return B_OK on success, or B_NO_INIT if internal state is not valid.
 */
status_t
BJoystick::RescanDevices()
{
	CALLED();

	if (fDevices == NULL)
		return B_NO_INIT;

	ScanDevices(true);
	return B_OK;
}


/**
 * @brief Switches the joystick to enhanced (extended) mode.
 * @param ref Optional entry_ref for the joystick description; may be NULL.
 * @return \c true if enhanced mode is now active.
 */
bool
BJoystick::EnterEnhancedMode(const entry_ref *ref)
{
	CALLED();
	fBeBoxMode = false;
	return !fBeBoxMode;
}


/**
 * @brief Returns the number of sticks reported by the open device.
 * @return The stick count, or 0 if no device is open.
 */
int32
BJoystick::CountSticks()
{
	CALLED();
	if (fJoystickInfo == NULL)
		return 0;

	return fJoystickInfo->module_info.num_sticks;
}


/**
 * @brief Returns the number of axes per stick reported by the open device.
 * @return The axis count, or 0 if no device is open.
 */
int32
BJoystick::CountAxes()
{
	CALLED();
	if (fJoystickInfo == NULL)
		return 0;

	return fJoystickInfo->module_info.num_axes;
}


/**
 * @brief Reads the current axis values for the specified stick.
 * @param outValues Array of int16 to receive the axis values; must have at
 *     least CountAxes() elements.
 * @param forStick Zero-based index of the stick to read.
 * @return B_OK on success, B_BAD_INDEX if \a forStick is out of range, or
 *     B_NO_INIT if the device is not open.
 */
status_t
BJoystick::GetAxisValues(int16 *outValues, int32 forStick)
{
	CALLED();

	if (fJoystickInfo == NULL || fJoystickData == NULL)
		return B_NO_INIT;

	if (forStick < 0
		|| forStick >= (int32)fJoystickInfo->module_info.num_sticks)
		return B_BAD_INDEX;

	variable_joystick *variableJoystick
		= (variable_joystick *)fJoystickData->ItemAt(forStick);
	if (variableJoystick == NULL)
		return B_NO_INIT;

	memcpy(outValues, variableJoystick->axes,
		fJoystickInfo->module_info.num_axes * sizeof(uint16));
	return B_OK;
}


/**
 * @brief Retrieves the human-readable name of the axis at the given index.
 * @param index Zero-based axis index.
 * @param outName BString to receive the name.
 * @return B_OK on success, B_BAD_INDEX if \a index is out of range, or
 *     B_BAD_VALUE if \a outName is NULL.
 */
status_t
BJoystick::GetAxisNameAt(int32 index, BString *outName)
{
	CALLED();

	if (index >= CountAxes())
		return B_BAD_INDEX;

	if (outName == NULL)
		return B_BAD_VALUE;

	// TODO: actually retrieve the name from the driver (via a new ioctl)
	*outName = "Axis ";
	*outName << index;
	return B_OK;
}


/**
 * @brief Returns the number of hats per stick reported by the open device.
 * @return The hat count, or 0 if no device is open.
 */
int32
BJoystick::CountHats()
{
	CALLED();
	if (fJoystickInfo == NULL)
		return 0;

	return fJoystickInfo->module_info.num_hats;
}


/**
 * @brief Reads the current hat positions for the specified stick.
 * @param outHats Array of uint8 to receive hat values; must have at least
 *     CountHats() elements.
 * @param forStick Zero-based index of the stick to read.
 * @return B_OK on success, B_BAD_INDEX if \a forStick is out of range, or
 *     B_NO_INIT if the device is not open.
 */
status_t
BJoystick::GetHatValues(uint8 *outHats, int32 forStick)
{
	CALLED();

	if (fJoystickInfo == NULL || fJoystickData == NULL)
		return B_NO_INIT;

	if (forStick < 0
		|| forStick >= (int32)fJoystickInfo->module_info.num_sticks)
		return B_BAD_INDEX;

	variable_joystick *variableJoystick
		= (variable_joystick *)fJoystickData->ItemAt(forStick);
	if (variableJoystick == NULL)
		return B_NO_INIT;

	memcpy(outHats, variableJoystick->hats,
		fJoystickInfo->module_info.num_hats);
	return B_OK;
}


/**
 * @brief Retrieves the human-readable name of the hat at the given index.
 * @param index Zero-based hat index.
 * @param outName BString to receive the name.
 * @return B_OK on success, B_BAD_INDEX if \a index is out of range, or
 *     B_BAD_VALUE if \a outName is NULL.
 */
status_t
BJoystick::GetHatNameAt(int32 index, BString *outName)
{
	CALLED();

	if (index >= CountHats())
		return B_BAD_INDEX;

	if (outName == NULL)
		return B_BAD_VALUE;

	// TODO: actually retrieve the name from the driver (via a new ioctl)
	*outName = "Hat ";
	*outName << index;
	return B_OK;
}


/**
 * @brief Returns the number of buttons per stick reported by the open device.
 * @return The button count, or 0 if no device is open.
 */
int32
BJoystick::CountButtons()
{
	CALLED();
	if (fJoystickInfo == NULL)
		return 0;

	return fJoystickInfo->module_info.num_buttons;
}


/**
 * @brief Returns the raw button bitmask for the specified stick.
 *
 * Each bit corresponds to one button; a set bit means the button is pressed.
 *
 * @param forStick Zero-based stick index.
 * @return The button bitmask, or 0 if the stick index is invalid or no
 *     device is open.
 */
uint32
BJoystick::ButtonValues(int32 forStick)
{
	CALLED();

	if (fJoystickInfo == NULL || fJoystickData == NULL)
		return 0;

	if (forStick < 0
		|| forStick >= (int32)fJoystickInfo->module_info.num_sticks)
		return 0;

	variable_joystick *variableJoystick
		= (variable_joystick *)fJoystickData->ItemAt(forStick);
	if (variableJoystick == NULL || variableJoystick->button_blocks == 0)
		return 0;

	return *variableJoystick->buttons;
}


/**
 * @brief Reads the current pressed/released state of all buttons.
 * @param outButtons Array of bool to receive button states; must have at
 *     least CountButtons() elements. \c true means pressed.
 * @param forStick Zero-based index of the stick to read.
 * @return B_OK on success, B_BAD_INDEX if \a forStick is invalid, or
 *     B_NO_INIT if the device is not open.
 */
status_t
BJoystick::GetButtonValues(bool *outButtons, int32 forStick)
{
	CALLED();

	if (fJoystickInfo == NULL || fJoystickData == NULL)
		return B_NO_INIT;

	if (forStick < 0
		|| forStick >= (int32)fJoystickInfo->module_info.num_sticks)
		return B_BAD_INDEX;

	variable_joystick *variableJoystick
		= (variable_joystick *)fJoystickData->ItemAt(forStick);
	if (variableJoystick == NULL)
		return B_NO_INIT;

	int16 buttonCount = fJoystickInfo->module_info.num_buttons;
	for (int16 i = 0; i < buttonCount; i++) {
		outButtons[i]
			= (variableJoystick->buttons[i / 32] & (1 << (i % 32))) != 0;
	}

	return B_OK;
}


/**
 * @brief Retrieves the human-readable name of the button at the given index.
 * @param index Zero-based button index.
 * @param outName BString to receive the name.
 * @return B_OK on success, B_BAD_INDEX if \a index is out of range, or
 *     B_BAD_VALUE if \a outName is NULL.
 */
status_t
BJoystick::GetButtonNameAt(int32 index, BString *outName)
{
	CALLED();

	if (index >= CountButtons())
		return B_BAD_INDEX;

	if (outName == NULL)
		return B_BAD_VALUE;

	// TODO: actually retrieve the name from the driver (via a new ioctl)
	*outName = "Button ";
	*outName << index;
	return B_OK;
}


/**
 * @brief Returns the name of the kernel module driving the open joystick.
 * @param outName BString to receive the module name.
 * @return B_OK on success, B_BAD_VALUE if \a outName is NULL, or B_NO_INIT
 *     if no device is open.
 */
status_t
BJoystick::GetControllerModule(BString *outName)
{
	CALLED();
	if (fJoystickInfo == NULL || fFD < 0)
		return B_NO_INIT;

	if (outName == NULL)
		return B_BAD_VALUE;

	outName->SetTo(fJoystickInfo->module_info.module_name);
	return B_OK;
}


/**
 * @brief Returns the human-readable name of the open joystick device.
 * @param outName BString to receive the device name.
 * @return B_OK on success, B_BAD_VALUE if \a outName is NULL, or B_NO_INIT
 *     if no device is open.
 */
status_t
BJoystick::GetControllerName(BString *outName)
{
	CALLED();
	if (fJoystickInfo == NULL || fFD < 0)
		return B_NO_INIT;

	if (outName == NULL)
		return B_BAD_VALUE;

	outName->SetTo(fJoystickInfo->module_info.device_name);
	return B_OK;
}


/**
 * @brief Returns whether calibration is currently enabled for the open device.
 * @return \c true if calibration is enabled, \c false otherwise.
 */
bool
BJoystick::IsCalibrationEnabled()
{
	CALLED();
	if (fJoystickInfo == NULL)
		return false;

	return fJoystickInfo->calibration_enable;
}


/**
 * @brief Enables or disables calibration on the open joystick device.
 * @param calibrates \c true to enable calibration, \c false to disable it.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BJoystick::EnableCalibration(bool calibrates)
{
	CALLED();
	if (fJoystickInfo == NULL || fFD < 0)
		return B_NO_INIT;

	status_t result = ioctl(fFD, B_JOYSTICK_SET_RAW_MODE, &calibrates,
		sizeof(calibrates));
	if (result == B_OK)
		fJoystickInfo->calibration_enable = calibrates;

	return result;
}


/**
 * @brief Applies calibration data to an extended joystick reading.
 * @param reading Pointer to the extended joystick structure to calibrate.
 */
void
BJoystick::Calibrate(struct _extended_joystick *reading)
{
	CALLED();
}


/**
 * @brief Scans the joystick device directory and populates the device list.
 * @param useDisabled If \c true, includes disabled devices in the scan.
 */
void
BJoystick::ScanDevices(bool useDisabled)
{
	CALLED();
	if (useDisabled) {
		_BJoystickTweaker joystickTweaker(*this);
		joystickTweaker.scan_including_disabled();
	}
}


//	#pragma mark - FBC protection


void BJoystick::_ReservedJoystick1() {}
void BJoystick::_ReservedJoystick2() {}
void BJoystick::_ReservedJoystick3() {}
status_t BJoystick::_Reserved_Joystick_4(void*, ...) { return B_ERROR; }
status_t BJoystick::_Reserved_Joystick_5(void*, ...) { return B_ERROR; }
status_t BJoystick::_Reserved_Joystick_6(void*, ...) { return B_ERROR; }
