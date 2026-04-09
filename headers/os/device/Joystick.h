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
 *   Copyright 2009, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file Joystick.h
 *  @brief Joystick device interface with optional enhanced (multi-axis/button) mode. */

#ifndef	_JOYSTICK_H
#define	_JOYSTICK_H


#include <OS.h>
#include <SupportDefs.h>


class BList;
class BString;
class _BJoystickTweaker;
struct entry_ref;
struct _extended_joystick;
struct _joystick_info;


/** @brief Provides access to joystick devices, including standard and enhanced modes. */
class BJoystick {
public:
							BJoystick();
	virtual					~BJoystick();

	/** @brief Opens the joystick device at the given port in standard mode.
	 *  @param portName Path to the joystick port.
	 *  @return B_OK on success, or an error code. */
			status_t		Open(const char* portName);

	/** @brief Opens the joystick device with optional enhanced mode.
	 *  @param portName Path to the joystick port.
	 *  @param enhanced If true, open in enhanced (multi-axis/button) mode.
	 *  @return B_OK on success, or an error code. */
			status_t		Open(const char* portName, bool enhanced);

	/** @brief Closes the joystick device. */
			void			Close();

	/** @brief Reads current joystick state into public member fields.
	 *  @return B_OK on success, or an error code. */
			status_t		Update();

	/** @brief Sets the maximum input latency for joystick reads.
	 *  @param maxLatency Maximum latency in microseconds.
	 *  @return B_OK on success, or an error code. */
			status_t		SetMaxLatency(bigtime_t maxLatency);

			bigtime_t		timestamp;  ///< Timestamp of the last Update() call.
			int16			horizontal; ///< Horizontal axis value from the last Update().
			int16			vertical;   ///< Vertical axis value from the last Update().

			bool			button1;    ///< State of button 1 from the last Update().
			bool			button2;    ///< State of button 2 from the last Update().

	/** @brief Returns the number of available joystick devices.
	 *  @return Device count. */
			int32			CountDevices();

	/** @brief Retrieves the name of a joystick device by index.
	 *  @param index Zero-based device index.
	 *  @param name Buffer to receive the name.
	 *  @param bufSize Size of the name buffer.
	 *  @return B_OK on success, or an error code. */
			status_t		GetDeviceName(int32 index, char* name,
								size_t bufSize = B_OS_NAME_LENGTH);

	/** @brief Re-enumerates joystick devices to detect newly connected hardware.
	 *  @return B_OK on success, or an error code. */
			status_t		RescanDevices();
								// Haiku extension. Updates the list of devices
								// as enumerated by CountDevices() and
								// GetDeviceName() with possibly newly plugged
								// in devices.

	/** @brief Switches to enhanced mode using an optional device reference.
	 *  @param ref Optional entry_ref identifying the device; NULL uses the current device.
	 *  @return true if enhanced mode was successfully entered. */
			bool			EnterEnhancedMode(const entry_ref* ref = NULL);

	/** @brief Returns the number of sticks reported in enhanced mode.
	 *  @return Stick count. */
			int32			CountSticks();

	/** @brief Returns the number of axes available in enhanced mode.
	 *  @return Axis count. */
			int32			CountAxes();

	/** @brief Reads all axis values for a given stick.
	 *  @param outValues Array receiving axis values; must have CountAxes() elements.
	 *  @param forStick Zero-based stick index.
	 *  @return B_OK on success, or an error code. */
			status_t		GetAxisValues(int16* outValues,
								int32 forStick = 0);

	/** @brief Retrieves the name of an axis by index.
	 *  @param index Zero-based axis index.
	 *  @param outName String to receive the axis name.
	 *  @return B_OK on success, or an error code. */
			status_t		GetAxisNameAt(int32 index,
								BString* outName);

	/** @brief Returns the number of hat switches available.
	 *  @return Hat count. */
			int32			CountHats();

	/** @brief Reads all hat values for a given stick.
	 *  @param outHats Array receiving hat values; must have CountHats() elements.
	 *  @param forStick Zero-based stick index.
	 *  @return B_OK on success, or an error code. */
			status_t		GetHatValues(uint8* outHats,
								int32 forStick = 0);

	/** @brief Retrieves the name of a hat switch by index.
	 *  @param index Zero-based hat index.
	 *  @param outName String to receive the hat name.
	 *  @return B_OK on success, or an error code. */
			status_t		GetHatNameAt(int32 index, BString* outName);

	/** @brief Returns the number of buttons available.
	 *  @return Button count. */
			int32			CountButtons();

	/** @brief Returns a bitmask of the first 32 button states for a stick.
	 *  @param forStick Zero-based stick index.
	 *  @return Bitmask where each set bit indicates a pressed button. */
			uint32			ButtonValues(int32 forStick = 0);
								// Allows access to the first 32 buttons where
								// each set bit indicates a pressed button.

	/** @brief Reads all button states for a given stick.
	 *  @param outButtons Array receiving button states; must have CountButtons() elements.
	 *  @param forStick Zero-based stick index.
	 *  @return B_OK on success, or an error code. */
			status_t		GetButtonValues(bool* outButtons,
								int32 forStick = 0);
								// Haiku extension. Allows to retrieve the state
								// of an arbitrary count of buttons. The
								// outButtons argument is an array of boolean
								// values with at least CountButtons() elements.
								// True means the button is pressed and false
								// means it is released.

	/** @brief Retrieves the name of a button by index.
	 *  @param index Zero-based button index.
	 *  @param outName String to receive the button name.
	 *  @return B_OK on success, or an error code. */
			status_t		GetButtonNameAt(int32 index,
								BString* outName);

	/** @brief Gets the name of the controller module driving this device.
	 *  @param outName String to receive the module name.
	 *  @return B_OK on success, or an error code. */
			status_t		GetControllerModule(BString* outName);

	/** @brief Gets the human-readable name of the controller.
	 *  @param outName String to receive the controller name.
	 *  @return B_OK on success, or an error code. */
			status_t		GetControllerName(BString* outName);

	/** @brief Returns whether axis calibration is currently enabled.
	 *  @return true if calibration is active. */
			bool			IsCalibrationEnabled();

	/** @brief Enables or disables axis calibration.
	 *  @param calibrates If true, enable calibration; false to disable.
	 *  @return B_OK on success, or an error code. */
			status_t		EnableCalibration(bool calibrates = true);

protected:
	/** @brief Override to apply custom axis calibration to joystick data.
	 *  @param joystick Pointer to the extended joystick structure to modify. */
	virtual	void			Calibrate(struct _extended_joystick*);

private:
friend class _BJoystickTweaker;

			void			ScanDevices(bool useDisabled = false);

			void            _ReservedJoystick1();
	virtual void            _ReservedJoystick2();
	virtual void            _ReservedJoystick3();
	virtual status_t        _Reserved_Joystick_4(void *, ...);
	virtual status_t        _Reserved_Joystick_5(void *, ...);
	virtual status_t        _Reserved_Joystick_6(void *, ...);

			bool			fBeBoxMode;
			bool			fReservedBool;
			int				fFD;
			BList*			fDevices;
			_joystick_info*	fJoystickInfo;
			BList*			fJoystickData;

			uint32          _reserved_Joystick_[10];
};

#endif // _JOYSTICK_H
