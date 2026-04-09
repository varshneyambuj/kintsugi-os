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

/** @file SerialPort.h
 *  @brief Serial port device interface with configurable baud rate, framing, and flow control. */

#ifndef	_SERIAL_PORT_H
#define	_SERIAL_PORT_H

#include <BeBuild.h>
#include <OS.h>
#include <SupportDefs.h>

#include <stddef.h>

class BList;

/** @brief Supported serial baud rates. */
enum data_rate {
	B_0_BPS = 0,
	B_50_BPS,
	B_75_BPS,
	B_110_BPS,
	B_134_BPS,
	B_150_BPS,
	B_200_BPS,
	B_300_BPS,
	B_600_BPS,
	B_1200_BPS,
	B_1800_BPS,
	B_2400_BPS,
	B_4800_BPS,
	B_9600_BPS,
	B_19200_BPS,
	B_38400_BPS,
	B_57600_BPS,
	B_115200_BPS,
	B_230400_BPS,
	B_31250_BPS
};

/** @brief Number of data bits per character. */
enum data_bits {
	B_DATA_BITS_7,
	B_DATA_BITS_8
};

/** @brief Number of stop bits per character. */
enum stop_bits {
	B_STOP_BITS_1,
	B_STOP_BITS_2
};

#define B_STOP_BIT_1	B_STOP_BITS_1

/** @brief Parity checking mode. */
enum parity_mode {
	B_NO_PARITY,
	B_ODD_PARITY,
	B_EVEN_PARITY
};

/** @brief Flow control method flags. */
enum {
	B_NOFLOW_CONTROL = 0,
	B_HARDWARE_CONTROL = 0x00000001,
	B_SOFTWARE_CONTROL = 0x00000002
};


/** @brief Provides access to a serial port with configurable communication parameters. */
class BSerialPort {
public:
							BSerialPort();
	virtual					~BSerialPort();

	/** @brief Opens the serial port at the given name.
	 *  @param portName Path to the serial port device.
	 *  @return B_OK on success, or an error code. */
			status_t		Open(const char* portName);

	/** @brief Closes the serial port. */
			void			Close();

	/** @brief Reads up to count bytes from the port.
	 *  @param buf Buffer to receive data.
	 *  @param count Maximum number of bytes to read.
	 *  @return Number of bytes read, or a negative error code. */
			ssize_t			Read(void* buf, size_t count);

	/** @brief Writes count bytes to the port.
	 *  @param buf Data to send.
	 *  @param count Number of bytes to write.
	 *  @return Number of bytes written, or a negative error code. */
			ssize_t			Write(const void* buf, size_t count);

	/** @brief Enables or disables blocking I/O.
	 *  @param blocking If true, reads block until data is available. */
			void			SetBlocking(bool blocking);

	/** @brief Sets the read timeout.
	 *  @param microSeconds Timeout in microseconds.
	 *  @return B_OK on success, or an error code. */
			status_t		SetTimeout(bigtime_t microSeconds);

	/** @brief Sets the baud rate.
	 *  @param bitsPerSecond Desired baud rate from the data_rate enum.
	 *  @return B_OK on success, or an error code. */
			status_t		SetDataRate(data_rate bitsPerSecond);

	/** @brief Returns the current baud rate.
	 *  @return Current data_rate value. */
			data_rate		DataRate();

	/** @brief Sets the number of data bits per character.
	 *  @param numBits Desired data_bits value. */
			void			SetDataBits(data_bits numBits);

	/** @brief Returns the current data bits setting.
	 *  @return Current data_bits value. */
			data_bits		DataBits();

	/** @brief Sets the number of stop bits per character.
	 *  @param numBits Desired stop_bits value. */
			void			SetStopBits(stop_bits numBits);

	/** @brief Returns the current stop bits setting.
	 *  @return Current stop_bits value. */
			stop_bits		StopBits();

	/** @brief Sets the parity mode.
	 *  @param which Desired parity_mode value. */
			void			SetParityMode(parity_mode which);

	/** @brief Returns the current parity mode.
	 *  @return Current parity_mode value. */
			parity_mode		ParityMode();

	/** @brief Discards any data waiting in the input buffer. */
			void			ClearInput();

	/** @brief Discards any data waiting in the output buffer. */
			void			ClearOutput();

	/** @brief Sets the flow control method.
	 *  @param method Bitfield of flow control flags (B_HARDWARE_CONTROL, B_SOFTWARE_CONTROL). */
			void			SetFlowControl(uint32 method);

	/** @brief Returns the current flow control method.
	 *  @return Current flow control bitfield. */
			uint32			FlowControl();

	/** @brief Asserts or de-asserts the DTR signal.
	 *  @param asserted True to assert DTR.
	 *  @return B_OK on success, or an error code. */
			status_t		SetDTR(bool asserted);

	/** @brief Asserts or de-asserts the RTS signal.
	 *  @param asserted True to assert RTS.
	 *  @return B_OK on success, or an error code. */
			status_t		SetRTS(bool asserted);

	/** @brief Returns the number of characters available in the input buffer.
	 *  @param waitThisMany Set to the number of bytes currently available.
	 *  @return B_OK on success, or an error code. */
			status_t		NumCharsAvailable(int32* waitThisMany);

	/** @brief Returns the state of the CTS (Clear To Send) line.
	 *  @return true if CTS is asserted. */
			bool			IsCTS();

	/** @brief Returns the state of the DSR (Data Set Ready) line.
	 *  @return true if DSR is asserted. */
			bool			IsDSR();

	/** @brief Returns the state of the RI (Ring Indicator) line.
	 *  @return true if RI is asserted. */
			bool			IsRI();

	/** @brief Returns the state of the DCD (Data Carrier Detect) line.
	 *  @return true if DCD is asserted. */
			bool			IsDCD();

	/** @brief Blocks until input data is available.
	 *  @return Number of bytes available, or a negative error code. */
			ssize_t			WaitForInput();

	/** @brief Returns the number of available serial port devices.
	 *  @return Device count. */
			int32			CountDevices();

	/** @brief Retrieves the name of a serial port device by index.
	 *  @param index Zero-based device index.
	 *  @param name Buffer to receive the device name.
	 *  @param bufSize Size of the name buffer.
	 *  @return B_OK on success, or an error code. */
			status_t		GetDeviceName(int32 index, char* name,
								size_t bufSize = B_OS_NAME_LENGTH);

private:
			void			_ScanDevices();
			int				_DriverControl();
	virtual	void			_ReservedSerialPort1();
	virtual	void			_ReservedSerialPort2();
	virtual	void			_ReservedSerialPort3();
	virtual	void			_ReservedSerialPort4();

			int				ffd;
			data_rate		fBaudRate;
			data_bits		fDataBits;
			stop_bits		fStopBits;
			parity_mode		fParityMode;
			uint32			fFlow;
			bigtime_t		fTimeout;
			bool			fBlocking;
			BList*			fDevices;
			uint32			fReserved[3];
};

#endif //_SERIAL_PORT_H
