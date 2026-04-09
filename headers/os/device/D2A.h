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

/** @file D2A.h
 *  @brief Digital-to-Analog converter device interface. */

#ifndef	_D2A_H
#define	_D2A_H

#include <BeBuild.h>
#include <SupportDefs.h>

#include <stddef.h>

/** @brief Provides access to a digital-to-analog converter device. */
class BD2A {
public:
							BD2A();
	virtual					~BD2A();

	/** @brief Opens the D2A device at the given port.
	 *  @param portName Path to the device port.
	 *  @return B_OK on success, or an error code. */
			status_t		Open(const char* portName);

	/** @brief Closes the D2A device. */
			void			Close();

	/** @brief Returns whether the device is currently open.
	 *  @return true if open, false otherwise. */
			bool			IsOpen();

	/** @brief Reads the current output value from the D2A device.
	 *  @param buf Buffer to receive the byte value.
	 *  @return Number of bytes read, or a negative error code. */
			ssize_t			Read(uint8* buf);

	/** @brief Writes a value to the D2A device.
	 *  @param value The byte value to output.
	 *  @return Number of bytes written, or a negative error code. */
			ssize_t			Write(uint8 value);

private:
	virtual	void			_ReservedD2A1();
	virtual	void			_ReservedD2A2();
	virtual	void			_ReservedD2A3();

			int				fFd;
			uint32			_fReserved[3];
};

#endif //_D2A_H
