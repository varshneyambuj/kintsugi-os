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
   Copyright 2006-2010, Haiku, Inc. All Rights Reserved.
   Distributed under the terms of the MIT License.
   
   Authors:
   		Axel Dörfler, axeld@pinc-software.de
 */
/** @file DHCPClient.h
 *  @brief DHCP client that negotiates IP configuration with a server. */
#ifndef DHCP_CLIENT_H
#define DHCP_CLIENT_H


#include "AutoconfigClient.h"

#include <netinet/in.h>

#include <NetworkAddress.h>


class BMessageRunner;
struct dhcp_message;
struct socket_timeout;


enum dhcp_state {
	INIT,
	SELECTING,
	INIT_REBOOT,
	REBOOTING,
	REQUESTING,
	BOUND,
	RENEWING,
	REBINDING,
};


/** @brief DHCP protocol client that negotiates IP leases. */
class DHCPClient : public AutoconfigClient {
public:
								/** @brief Construct a DHCP client for a target and network device. */
								DHCPClient(BMessenger target,
									const char* device);
	/** @brief Release the DHCP lease and clean up. */
	virtual						~DHCPClient();

	/** @brief Spawn the negotiator thread and begin DHCP. */
	virtual	status_t			Start();

	/** @brief Handle lease-time renewal messages. */
	virtual	void				MessageReceived(BMessage* message);

private:
	static	status_t			_NegotiatorThread(void* data);
			status_t			_Negotiate();
			status_t			_GotMessage(dhcp_state& state,
									dhcp_message* message);
			status_t			_StateTransition(int socket, dhcp_state& state);
			void				_ParseOptions(dhcp_message& message,
									BMessage& address,
									BMessage& resolverConfiguration);
			void				_PrepareMessage(dhcp_message& message,
									dhcp_state state);
			status_t			_SendMessage(int socket, dhcp_message& message,
									const BNetworkAddress& address) const;
			dhcp_state			_CurrentState() const;
			bool				_TimeoutShift(int socket, dhcp_state& state,
									socket_timeout& timeout);
			void				_RestartLease(bigtime_t lease);

	static	BString				_AddressToString(const uint8* data);
	static 	BString				_AddressToString(in_addr_t address);

private:
			BMessage			fConfiguration; /**< Interface configuration message */
			BMessage			fResolverConfiguration; /**< DNS resolver configuration message */
			BMessageRunner*		fRunner; /**< Lease renewal message runner */
			thread_id			fNegotiateThread; /**< DHCP negotiation thread ID */
			uint8				fMAC[6]; /**< Hardware MAC address */
			BString				fHostName; /**< System host name for DHCP requests */
			uint32				fTransactionID; /**< Current DHCP transaction identifier */
			in_addr_t			fAssignedAddress; /**< IP address assigned by the server */
			BNetworkAddress		fServer; /**< DHCP server address */
			bigtime_t			fStartTime;
			bigtime_t			fRequestTime;
			bigtime_t			fRenewalTime; /**< Absolute renewal time in microseconds */
			bigtime_t			fRebindingTime; /**< Absolute rebinding time in microseconds */
			bigtime_t			fLeaseTime; /**< Absolute lease expiry time in microseconds */
			status_t			fStatus; /**< Current negotiation status */
};

#endif	// DHCP_CLIENT_H
