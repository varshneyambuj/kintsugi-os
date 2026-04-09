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
 *   Copyright 2002-2004, Marcus Overhagen, Stefano Ceccherini.
 *   All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SerialPort.cpp
 * @brief Serial port interface for the Device Kit
 *
 * Implements BSerialPort, which provides access to RS-232 serial ports.
 * Supports configurable baud rate, data bits, stop bits, parity, flow
 * control, and blocking/timeout behaviour. Device enumeration scans
 * /dev/ports at construction time and on explicit CountDevices() calls.
 *
 * @see BJoystick, BDigitalPort
 */


#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <List.h>
#include <SerialPort.h>

#include <new>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>


/* The directory where the serial driver publishes its devices */
#define SERIAL_DIR "/dev/ports"


/**
 * @brief Scans a directory and appends each entry's name to a BList.
 * @param directory Path of the directory to scan.
 * @param list BList of char* to receive the device names (caller owns memory).
 * @return The total number of items in \a list after scanning.
 */
static int32
scan_directory(const char *directory, BList *list)
{
	BEntry entry;
	BDirectory dir(SERIAL_DIR);
	char buf[B_OS_NAME_LENGTH];

	ASSERT(list != NULL);
	while (dir.GetNextEntry(&entry) == B_OK) {
		entry.GetName(buf);
		list->AddItem(strdup(buf));
	};

	return list->CountItems();
}


/**
 * @brief Creates and initializes a BSerialPort object.
 *
 * Queries the driver and builds a list of available serial ports.
 * The object is initialized with the following defaults:
 * - \c B_19200_BPS baud rate
 * - \c B_DATA_BITS_8
 * - \c B_STOP_BIT_1
 * - \c B_NO_PARITY
 * - \c B_HARDWARE_CONTROL flow control
 * - \c B_INFINITE_TIMEOUT
 * - Blocking mode enabled
 */
BSerialPort::BSerialPort()
	:
	ffd(-1),
	fBaudRate(B_19200_BPS),
	fDataBits(B_DATA_BITS_8),
	fStopBits(B_STOP_BIT_1),
	fParityMode(B_NO_PARITY),
	fFlow(B_HARDWARE_CONTROL),
	fTimeout(B_INFINITE_TIMEOUT),
	fBlocking(true),
	fDevices(new(std::nothrow) BList)
{
	_ScanDevices();
}


/**
 * @brief Destroys the BSerialPort object.
 *
 * Closes the port if it is open and frees the device name list.
 */
BSerialPort::~BSerialPort()
{
	if (ffd >= 0)
		close(ffd);

	if (fDevices != NULL) {
		for (int32 count = fDevices->CountItems() - 1; count >= 0; count--)
			free(fDevices->RemoveItem(count));
		delete fDevices;
	}
}


/**
 * @brief Opens a serial port by name.
 *
 * Accepts both short names (e.g. "serial2") and absolute paths
 * (e.g. "/dev/ports/serial2"). If the port is already open, closes it
 * first. Clears O_NONBLOCK after opening so that subsequent read/write
 * calls can block as configured.
 *
 * @param portName Name or path of the serial port to open.
 * @return A positive file descriptor on success, or \c errno on failure.
 */
status_t
BSerialPort::Open(const char *portName)
{
	char buf[64];

	if (portName == NULL)
		return B_BAD_VALUE; // Heheee, we won't crash

	if (portName[0] != '/')
		snprintf(buf, 64, SERIAL_DIR"/%s", portName);
	else
		// A name like "/dev/ports/serial2" was passed
		snprintf(buf, 64, "%s", portName);

	if (ffd >= 0) //If this port is already open, close it
		close(ffd);

	// TODO: BeOS don't use O_EXCL, and this seems to lead
	// to some issues. I added this flag having read some comments
	// by Marco Nelissen on the annotated BeBook.
	// I think BeOS uses O_RDWR|O_NONBLOCK here.
	ffd = open(buf, O_RDWR | O_NONBLOCK | O_EXCL);

	if (ffd >= 0) {
		// we used open() with O_NONBLOCK flag to let it return immediately,
		// but we want read/write operations to block if needed,
		// so we clear that bit here.
		int flags = fcntl(ffd, F_GETFL);
		fcntl(ffd, F_SETFL, flags & ~O_NONBLOCK);

		_DriverControl();
	}
	// TODO: I wonder why the return type is a status_t,
	// since we (as BeOS does) return the descriptor number for the device...
	return (ffd >= 0) ? ffd : errno;
}


/** @brief Closes the serial port and resets the file descriptor. */
void
BSerialPort::Close(void)
{
	if (ffd >= 0)
		close(ffd);
	ffd = -1;
}


/**
 * @brief Reads bytes from the serial port.
 * @param buf Destination buffer for the received data.
 * @param count Maximum number of bytes to read.
 * @return The number of bytes read, or \c errno on failure.
 */
ssize_t
BSerialPort::Read(void *buf, size_t count)
{
	ssize_t err = read(ffd, buf, count);

	return (err >= 0) ? err : errno;
}


/**
 * @brief Writes bytes to the serial port.
 * @param buf Source buffer containing the data to send.
 * @param count Number of bytes to write.
 * @return The number of bytes written, or \c errno on failure.
 */
ssize_t
BSerialPort::Write(const void *buf, size_t count)
{
	ssize_t err = write(ffd, buf, count);

	return (err >= 0) ? err : errno;
}


/**
 * @brief Enables or disables blocking I/O mode.
 * @param Blocking \c true to enable blocking mode; \c false for non-blocking.
 */
void
BSerialPort::SetBlocking(bool Blocking)
{
	fBlocking = Blocking;
	_DriverControl();
}


/**
 * @brief Sets the read timeout for the port.
 *
 * Valid values are \c B_INFINITE_TIMEOUT or any value from 0 to 25,000,000
 * microseconds. The serial driver has a granularity of 100,000 microseconds.
 *
 * @param microSeconds The timeout in microseconds.
 * @return B_OK on success, or B_BAD_VALUE if \a microSeconds is invalid.
 */
status_t
BSerialPort::SetTimeout(bigtime_t microSeconds)
{
	status_t err = B_BAD_VALUE;

	if (microSeconds == B_INFINITE_TIMEOUT || microSeconds <= 25000000) {
		fTimeout = microSeconds;
		_DriverControl();
		err = B_OK;
	}
	return err;
}


/**
 * @brief Sets the baud rate (data rate) for the port.
 *
 * Valid values include \c B_0_BPS through \c B_230400_BPS and \c B_31250_BPS.
 *
 * @param bitsPerSecond The desired baud rate constant.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSerialPort::SetDataRate(data_rate bitsPerSecond)
{
	fBaudRate = bitsPerSecond;

	return _DriverControl();
}


/**
 * @brief Returns the current baud rate.
 * @return The current \c data_rate setting.
 */
data_rate
BSerialPort::DataRate(void)
{
	return fBaudRate;
}


/**
 * @brief Sets the number of data bits per character (7 or 8).
 * @param numBits The data bits constant (\c B_DATA_BITS_7 or
 *     \c B_DATA_BITS_8).
 */
void
BSerialPort::SetDataBits(data_bits numBits)
{
	fDataBits = numBits;
	_DriverControl();
}


/**
 * @brief Returns the current data bits setting.
 * @return The current \c data_bits value.
 */
data_bits
BSerialPort::DataBits(void)
{
	return fDataBits;
}


/**
 * @brief Sets the number of stop bits.
 *
 * Valid values are \c B_STOP_BIT_1 (or \c B_STOP_BITS_1) and
 * \c B_STOP_BITS_2.
 *
 * @param numBits The stop bits constant.
 */
void
BSerialPort::SetStopBits(stop_bits numBits)
{
	fStopBits = numBits;
	_DriverControl();
}


/**
 * @brief Returns the current stop bits setting.
 * @return The current \c stop_bits value.
 */
stop_bits
BSerialPort::StopBits(void)
{
	return fStopBits;
}


/**
 * @brief Sets the parity mode.
 *
 * Valid values are \c B_ODD_PARITY, \c B_EVEN_PARITY, and \c B_NO_PARITY.
 *
 * @param which The parity mode constant.
 */
void
BSerialPort::SetParityMode(parity_mode which)
{
	fParityMode = which;
	_DriverControl();
}


/**
 * @brief Returns the current parity mode.
 * @return The current \c parity_mode value.
 */
parity_mode
BSerialPort::ParityMode(void)
{
	return fParityMode;
}


/** @brief Discards all data in the input buffer. */
void
BSerialPort::ClearInput(void)
{
	tcflush(ffd, TCIFLUSH);
}


/** @brief Discards all data in the output buffer. */
void
BSerialPort::ClearOutput(void)
{
	tcflush(ffd, TCOFLUSH);
}


/**
 * @brief Sets the flow control method.
 *
 * Valid values are \c B_HARDWARE_CONTROL, \c B_SOFTWARE_CONTROL, and
 * \c B_NOFLOW_CONTROL.
 *
 * @param method The flow control constant.
 */
void
BSerialPort::SetFlowControl(uint32 method)
{
	fFlow = method;
	_DriverControl();
}


/**
 * @brief Returns the current flow control setting.
 * @return The current flow control bitmask.
 */
uint32
BSerialPort::FlowControl(void)
{
	return fFlow;
}


/**
 * @brief Sets the Data Terminal Ready (DTR) signal.
 * @param asserted \c true to assert DTR, \c false to deassert.
 * @return B_OK on success, or \c errno on failure.
 */
status_t
BSerialPort::SetDTR(bool asserted)
{
	status_t status = ioctl(ffd, TCSETDTR, &asserted, sizeof(asserted));

	return (status >= 0) ? status : errno;
}


/**
 * @brief Sets the Request To Send (RTS) signal.
 * @param asserted \c true to assert RTS, \c false to deassert.
 * @return B_OK on success, or \c errno on failure.
 */
status_t
BSerialPort::SetRTS(bool asserted)
{
	status_t status = ioctl(ffd, TCSETRTS, &asserted, sizeof(asserted));

	return (status >= 0) ? status : errno;
}


/**
 * @brief Returns the number of bytes waiting in the input queue.
 * @param numChars Pointer to an int32 to receive the byte count.
 * @return B_OK on success, or B_NO_INIT if the port is not open.
 */
status_t
BSerialPort::NumCharsAvailable(int32 *numChars)
{
	//No help from the BeBook...
	if (ffd < 0)
		return B_NO_INIT;

	// TODO: Implement ?
	if (numChars)
		*numChars = 0;
	return B_OK;
}


/**
 * @brief Returns whether the Clear To Send (CTS) pin is asserted.
 * @return \c true if CTS is asserted, \c false otherwise.
 */
bool
BSerialPort::IsCTS(void)
{
	unsigned int bits = ioctl(ffd, TCGETBITS, 0);

	if (bits & TCGB_CTS)
		return true;

	return false;
}


/**
 * @brief Returns whether the Data Set Ready (DSR) pin is asserted.
 * @return \c true if DSR is asserted, \c false otherwise.
 */
bool
BSerialPort::IsDSR(void)
{
	unsigned int bits = ioctl(ffd, TCGETBITS, 0);

	if (bits & TCGB_DSR)
		return true;

	return false;
}


/**
 * @brief Returns whether the Ring Indicator (RI) pin is asserted.
 * @return \c true if RI is asserted, \c false otherwise.
 */
bool
BSerialPort::IsRI(void)
{
	unsigned int bits = ioctl(ffd, TCGETBITS, 0);

	if (bits & TCGB_RI)
		return true;

	return false;
}


/**
 * @brief Returns whether the Data Carrier Detect (DCD) pin is asserted.
 * @return \c true if DCD is asserted, \c false otherwise.
 */
bool
BSerialPort::IsDCD(void)
{
	unsigned int bits = ioctl(ffd, TCGETBITS, 0);

	if (bits & TCGB_DCD)
		return true;

	return false;
}


/**
 * @brief Blocks until data is available to read or the timeout elapses.
 *
 * Always blocks regardless of the SetBlocking() setting, but respects the
 * timeout set by SetTimeout().
 *
 * @return The number of bytes available to read, or a negative error code.
 */
ssize_t
BSerialPort::WaitForInput(void)
{
	object_wait_info info[1];
	info[0].type = B_OBJECT_TYPE_FD;
	info[0].object = ffd;
	info[0].events = B_EVENT_READ | B_EVENT_ERROR | B_EVENT_DISCONNECTED;
	status_t status = wait_for_objects_etc(info, 1, B_RELATIVE_TIMEOUT, fTimeout);
	if (status < 0)
		return status;

	int size;
	if (ioctl(ffd, FIONREAD, &size, sizeof(size)) < 0)
		return errno;
	return size;
}


/**
 * @brief Returns the number of serial port devices available on the system.
 *
 * Triggers a rescan of /dev/ports before returning the count.
 *
 * @return The number of available serial ports.
 */
int32
BSerialPort::CountDevices()
{
	int32 count = 0;

	// Refresh devices list
	_ScanDevices();

	if (fDevices != NULL)
		count = fDevices->CountItems();

	return count;
}


/**
 * @brief Returns the device name of the serial port at the given index.
 * @param n Zero-based index into the device list.
 * @param name Buffer to receive the null-terminated device name.
 * @param bufSize Size of \a name in bytes.
 * @return B_OK on success, or B_ERROR if \a n is out of range or the
 *     name or buffer pointer is invalid.
 */
status_t
BSerialPort::GetDeviceName(int32 n, char *name, size_t bufSize)
{
	status_t result = B_ERROR;
	const char *dev = NULL;

	if (fDevices != NULL)
		dev = static_cast<char*>(fDevices->ItemAt(n));

	if (dev != NULL && name != NULL) {
		strncpy(name, dev, bufSize);
		name[bufSize - 1] = '\0';
		result = B_OK;
	}
	return result;
}


//	#pragma mark - Private


/**
 * @brief Rebuilds the internal list of available serial port devices.
 *
 * Empties the current device list and scans SERIAL_DIR for new entries.
 */
void
BSerialPort::_ScanDevices()
{
	// First, we empty the list
	if (fDevices != NULL) {
		for (int32 count = fDevices->CountItems() - 1; count >= 0; count--)
			free(fDevices->RemoveItem(count));

		// Add devices to the list
		scan_directory(SERIAL_DIR, fDevices);
	}
}


/**
 * @brief Applies the current port settings to the serial driver via termios.
 *
 * Reads current termios attributes, resets all relevant flags, then sets
 * baud rate, data bits, stop bits, parity, flow control, and timeout
 * according to the current object state before writing back with tcsetattr().
 *
 * @return B_OK on success, or \c errno on failure.
 */
int
BSerialPort::_DriverControl()
{
	struct termios options;
	int err;

	if (ffd < 0)
		return B_NO_INIT;

	//Load the current settings
	err = tcgetattr(ffd, &options);
	if (err < 0)
		return errno;

	// Reset all flags
	options.c_iflag &= ~(IXON | IXOFF | IXANY | INPCK);
	options.c_cflag &= ~(CRTSCTS | CSIZE | CSTOPB | PARODD | PARENB);
	options.c_lflag &= ~(ECHO | ECHONL | ISIG | ICANON);

	// Local line
	options.c_cflag |= CLOCAL;

	//Set the flags to the wanted values
	if (fFlow & B_HARDWARE_CONTROL)
		options.c_cflag |= CRTSCTS;

	if (fFlow & B_SOFTWARE_CONTROL)
		options.c_iflag |= (IXON | IXOFF);

	if (fStopBits & B_STOP_BITS_2)
		options.c_cflag |= CSTOPB; // Set 2 stop bits

	if (fDataBits & B_DATA_BITS_8)
		options.c_cflag |= CS8; // Set 8 data bits

	//Ok, set the parity now
	if (fParityMode != B_NO_PARITY) {
		options.c_cflag |= PARENB; //Enable parity
		if (fParityMode == B_ODD_PARITY)
			options.c_cflag |= PARODD; //Select odd parity
	}

	//Set the baud rate
	cfsetispeed(&options, fBaudRate);
	cfsetospeed(&options, fBaudRate);

	//Set the timeout
	options.c_cc[VTIME] = 0;
	options.c_cc[VMIN] = 0;
	if (fBlocking) {
		if (fTimeout == B_INFINITE_TIMEOUT) {
			options.c_cc[VMIN] = 1;
		} else if (fTimeout != 0) {
			int timeout = fTimeout / 100000;
			options.c_cc[VTIME] = (timeout == 0) ? 1 : timeout;
		}
	}

	//Ok, finished. Now tell the driver what we decided
	err = tcsetattr(ffd, TCSANOW, &options);

	return (err >= 0) ? err : errno;
}


//	#pragma mark - FBC protection


void BSerialPort::_ReservedSerialPort1() {}
void BSerialPort::_ReservedSerialPort2() {}
void BSerialPort::_ReservedSerialPort3() {}
void BSerialPort::_ReservedSerialPort4() {}
