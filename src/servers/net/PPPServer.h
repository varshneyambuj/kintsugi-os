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
   Copyright 2004-2005, Waldemar Kornewald <wkornew@gmx.net>
   Distributed under the terms of the MIT License.
 */
/** @file PPPServer.h
 *  @brief Handler for managing PPP network interfaces. */
#ifndef _PPP_SERVER__H
#define _PPP_SERVER__H

#include <Handler.h>
#include <PPPInterfaceListener.h>


/** @brief Handler managing PPP dial-up network interfaces. */
class PPPServer : public BHandler {
	public:
		/** @brief Construct and begin watching the PPP manager. */
		PPPServer();
		/** @brief Stop watching and clean up interfaces. */
		virtual ~PPPServer();
		
		/** @brief Dispatch PPP report messages. */
		virtual void MessageReceived(BMessage *message);
			// the SimpleMessageFilter routes ppp_server messages to this handler

	private:
		void InitInterfaces();
		void UninitInterfaces();
		
		void HandleReportMessage(BMessage *message);
		void CreateConnectionRequestWindow(ppp_interface_id id);

	private:
		PPPInterfaceListener fListener; /**< PPP interface event listener */
};


#endif
