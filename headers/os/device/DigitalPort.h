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

/** @file DigitalPort.h
 *  @brief Digital I/O port device interface. */

#ifndef	_DIGITAL_PORT_H
#define	_DIGITAL_PORT_H

#include <BeBuild.h>
#include <SupportDefs.h>

#include <stddef.h>


/** @brief Provides access to a digital I/O port device, configurable as input or output. */
class BDigitalPort {
public:
							BDigitalPort();
	virtual					~BDigitalPort();

	/** @brief Opens the digital port at the given name.
	 *  @param portName Path to the device port.
	 *  @return B_OK on success, or an error code. */
			status_t		Open(const char* portName);

	/** @brief Closes the digital port. */
			void			Close();

	/** @brief Returns whether the port is currently open.
	 *  @return true if open, false otherwise. */
			bool			IsOpen();

	/** @brief Reads the current byte value from the port.
	 *  @param buf Buffer to receive the byte.
	 *  @return Number of bytes read, or a negative error code. */
			ssize_t			Read(uint8* buf);

	/** @brief Writes a byte value to the port.
	 *  @param value The byte to write.
	 *  @return Number of bytes written, or a negative error code. */
			ssize_t			Write(uint8 value);

	/** @brief Configures the port as an output.
	 *  @return B_OK on success, or an error code. */
			status_t		SetAsOutput();

	/** @brief Returns whether the port is configured as output.
	 *  @return true if output, false otherwise. */
			bool			IsOutput();

	/** @brief Configures the port as an input.
	 *  @return B_OK on success, or an error code. */
			status_t		SetAsInput();

	/** @brief Returns whether the port is configured as input.
	 *  @return true if input, false otherwise. */
			bool			IsInput();

private:
	virtual	void		_ReservedDigitalPort1();
	virtual	void		_ReservedDigitalPort2();
	virtual	void		_ReservedDigitalPort3();

			int			fFd;
			bool		fIsInput;
			uint32		_fReserved[3];
};

#endif // _DIGITAL_PORT_H
