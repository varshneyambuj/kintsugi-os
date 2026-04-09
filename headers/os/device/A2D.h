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

/** @file A2D.h
 *  @brief Analog-to-Digital converter device interface. */

#ifndef	_A2D_H
#define	_A2D_H

#include <BeBuild.h>
#include <SupportDefs.h>

#include <stddef.h>

/** @brief Provides access to an analog-to-digital converter device. */
class BA2D {
public:
							BA2D();
	virtual					~BA2D();

	/** @brief Opens the A2D device at the given port.
	 *  @param portName Path to the device port.
	 *  @return B_OK on success, or an error code. */
			status_t		Open(const char* portName);

	/** @brief Closes the A2D device. */
			void			Close();

	/** @brief Returns whether the device is currently open.
	 *  @return true if open, false otherwise. */
			bool			IsOpen();

	/** @brief Reads a sample from the A2D device.
	 *  @param buf Buffer to receive the unsigned short sample.
	 *  @return Number of bytes read, or a negative error code. */
			ssize_t			Read(ushort* buf);

private:
	virtual	void			_ReservedA2D1();
	virtual	void			_ReservedA2D2();
	virtual	void			_ReservedA2D3();

			int				fFd;
			uint32			_fReserved[3];
};

#endif // _A2D_H
