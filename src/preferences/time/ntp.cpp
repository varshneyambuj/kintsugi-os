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
 *   Copyright 2010-2011, Ryan Leavengood. All Rights Reserved.
 *   Copyright 2004-2009, pinc Software. All Rights Reserved.
 *   Distributed under the terms of the MIT license.
 */


/**
 * @file ntp.cpp
 * @brief SNTP client implementation; sends one request and applies the
 *        returned timestamp to the system clock.
 *
 * Implements RFC 1305 mode 3 (client) over UDP. Resolves the host, sends
 * an NTP packet, waits up to three seconds for a reply, parses the
 * transmit timestamp, and pushes the result through set_real_time_clock().
 */


#include "ntp.h"

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <OS.h>

#include <Catalog.h>
#include <NetworkAddress.h>
#include <NetworkAddressResolver.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


/* This structure and its data fields are described in RFC 1305
 * "Network Time Protocol (Version 3)" in appendix A.
 */

/**
 * @brief 32-bit signed fixed-point value used in NTP packets.
 */
struct fixed32 {
	int16	integer;
	uint16	fraction;

	/**
	 * @brief Stores @a integer and @a fraction in network byte order.
	 */
	void
	SetTo(int16 integer, uint16 fraction = 0)
	{
		this->integer = htons(integer);
		this->fraction = htons(fraction);
	}

	/** @brief Returns the integer part in host byte order. */
	int16 Integer() { return htons(integer); }
	/** @brief Returns the fractional part in host byte order. */
	uint16 Fraction() { return htons(fraction); }
};

/**
 * @brief 64-bit unsigned fixed-point value used in NTP timestamps.
 */
struct ufixed64 {
	uint32	integer;
	uint32	fraction;

	/**
	 * @brief Stores @a integer and @a fraction in network byte order.
	 */
	void
	SetTo(uint32 integer, uint32 fraction = 0)
	{
		this->integer = htonl(integer);
		this->fraction = htonl(fraction);
	}

	/** @brief Returns the integer part in host byte order. */
	uint32 Integer() { return htonl(integer); }
	/** @brief Returns the fractional part in host byte order. */
	uint32 Fraction() { return htonl(fraction); }
};

/**
 * @brief Wire format of an NTP version-3 packet.
 */
struct ntp_data {
	uint8		mode : 3;
	uint8		version : 3;
	uint8		leap_indicator : 2;

	uint8		stratum;
	int8		poll;
	int8		precision;	/* in seconds of the nearest power of two */

	fixed32		root_delay;
	fixed32		root_dispersion;
	uint32		root_identifier;

	ufixed64	reference_timestamp;
	ufixed64	originate_timestamp;
	ufixed64	receive_timestamp;
	ufixed64	transmit_timestamp;

	/* optional authenticator follows (96 bits) */
};

#define NTP_PORT		123
#define NTP_VERSION_3	3

/** @brief Leap-second indicator values (RFC 1305 appendix A). */
enum ntp_leap_warnings {
	LEAP_NO_WARNING = 0,
	LEAP_LAST_MINUTE_61_SECONDS,
	LEAP_LAST_MINUTE_59_SECONDS,
	LEAP_CLOCK_NOT_IN_SYNC,
};

/** @brief NTP association mode codes (RFC 1305 appendix A). */
enum ntp_modes {
	MODE_RESERVED = 0,
	MODE_SYMMETRIC_ACTIVE,
	MODE_SYMMETRIC_PASSIVE,
	MODE_CLIENT,
	MODE_SERVER,
	MODE_BROADCAST,
	MODE_NTP_CONTROL_MESSAGE,
};


/** @brief NTP epoch is 1900-01-01 UTC; UNIX epoch is 1970-01-01 UTC. */
const uint32 kSecondsBetween1900And1970 = 2208988800UL;


/**
 * @brief Returns the current real-time clock value in NTP epoch seconds.
 */
uint32
seconds_since_1900(void)
{
	return kSecondsBetween1900And1970 + real_time_clock();
}


/**
 * @brief Performs a single SNTP exchange and applies the returned time.
 *
 * Resolves @a hostname, opens a UDP socket, sends a client-mode NTP packet
 * with the current time as the transmit timestamp, and waits up to three
 * seconds for a reply via select(). On a valid reply, converts the
 * server-side transmit timestamp from NTP epoch to UNIX epoch and pushes it
 * to set_real_time_clock().
 *
 * @param hostname    Server hostname or IP.
 * @param errorString Output: human-readable error description on failure.
 * @param errorCode   Output: errno-style code on failure (when known).
 * @retval B_OK              On success.
 * @retval B_ENTRY_NOT_FOUND When the hostname could not be resolved.
 * @retval B_BAD_VALUE       When the server returned an invalid timestamp.
 * @retval B_ERROR           On socket / send / recv / select failure.
 */
status_t
ntp_update_time(const char* hostname, const char** errorString,
	int32* errorCode)
{
	BNetworkAddressResolver resolver(hostname, NTP_PORT);
	BNetworkAddress address;
	uint32 cookie = 0;
	bool success = false;

	if (resolver.InitCheck() != B_OK) {
		*errorString = B_TRANSLATE("Could not resolve server address");
		return B_ENTRY_NOT_FOUND;
	}

	ntp_data message;
	memset(&message, 0, sizeof(ntp_data));

	message.leap_indicator = LEAP_CLOCK_NOT_IN_SYNC;
	message.version = NTP_VERSION_3;
	message.mode = MODE_CLIENT;

	message.stratum = 1;	// primary reference
	message.precision = -5;	// 2^-5 ~ 32-64 Hz precision

	message.root_delay.SetTo(1);	// 1 sec
	message.root_dispersion.SetTo(1);

	message.transmit_timestamp.SetTo(seconds_since_1900());

	int connection = socket(AF_INET, SOCK_DGRAM, 0);
	if (connection < 0) {
		*errorString = B_TRANSLATE("Could not create socket");
		*errorCode = errno;
		return B_ERROR;
	}

	while (resolver.GetNextAddress(&cookie, address) == B_OK) {
		if (sendto(connection, reinterpret_cast<char*>(&message),
				sizeof(ntp_data), 0, &address.SockAddr(),
				address.Length()) != -1) {
			success = true;
			break;
		}
	}

	if (!success) {
		*errorString = B_TRANSLATE("Sending request failed");
		close(connection);
		return B_ERROR;
	}

	fd_set waitForReceived;
	FD_ZERO(&waitForReceived);
	FD_SET(connection, &waitForReceived);

	struct timeval timeout;
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	// we'll wait 3 seconds for the answer

	int status;
	do {
		status = select(connection + 1, &waitForReceived, NULL, NULL,
			&timeout);
	} while (status == -1 && errno == EINTR);
	if (status <= 0) {
		*errorString = B_TRANSLATE("Waiting for answer failed");
		*errorCode = errno;
		close(connection);
		return B_ERROR;
	}

	message.transmit_timestamp.SetTo(0);

	socklen_t addressSize = address.Length();
	if (recvfrom(connection, reinterpret_cast<char*>(&message), sizeof(ntp_data), 0,
			&address.SockAddr(), &addressSize) < (ssize_t)sizeof(ntp_data)) {
		*errorString = B_TRANSLATE("Message receiving failed");
		*errorCode = errno;
		close(connection);
		return B_ERROR;
	}

	close(connection);

	if (message.transmit_timestamp.Integer() == 0) {
		*errorString = B_TRANSLATE("Received invalid time");
		return B_BAD_VALUE;
	}

	time_t now = message.transmit_timestamp.Integer() - kSecondsBetween1900And1970;
	set_real_time_clock(now);
	return B_OK;
}
