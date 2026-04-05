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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005-2010, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file PortLink.cpp
 *  @brief BPrivate::PortLink implementation for port-based message transport.
 *
 *  Provides the base port link class that pairs a LinkSender and LinkReceiver
 *  on specified send and receive ports. Used as the underlying transport
 *  mechanism for app_server communication.
 */

#include <PortLink.h>


namespace BPrivate {


/** @brief Constructs a PortLink with the given send and receive ports.
 *
 *  Allocates a new LinkSender for the send port and a new LinkReceiver
 *  for the receive port.
 *
 *  @param send The port ID to use for sending messages.
 *  @param receive The port ID to use for receiving messages.
 */
PortLink::PortLink(port_id send, port_id receive)
{
	fSender = new LinkSender(send);
	fReceiver = new LinkReceiver(receive);
}


/** @brief Destroys the PortLink, freeing the sender and receiver. */
PortLink::~PortLink()
{
	delete fReceiver;
	delete fSender;
}


}	// namespace BPrivate
