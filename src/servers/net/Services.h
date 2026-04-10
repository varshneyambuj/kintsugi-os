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
   Copyright 2006-2015, Haiku, Inc. All Rights Reserved.
   Distributed under the terms of the MIT License.
   
   Authors:
   		Axel Dörfler, axeld@pinc-software.de
 */
/** @file Services.h
 *  @brief Network services manager that listens and dispatches connections. */
#ifndef SERVICES_H
#define SERVICES_H


#include <Handler.h>
#include <Locker.h>

#include <map>
#include <string>
#include <sys/select.h>


struct service;
struct service_connection;
typedef std::map<std::string, service*> ServiceNameMap;
typedef std::map<int, service_connection*> ServiceSocketMap;


/** @brief Inetd-style service manager listening on configured ports. */
class Services : public BHandler {
public:
								/** @brief Construct from a services configuration message. */
								Services(const BMessage& services);
	/** @brief Stop all running services and clean up. */
	virtual						~Services();

			/** @brief Return B_OK if the listener thread started successfully. */
			status_t			InitCheck() const;

	/** @brief Handle service update and status query messages. */
	virtual void				MessageReceived(BMessage* message);

private:
			void				_NotifyListener(bool quit = false);
			void				_UpdateMinMaxSocket(int socket);
			status_t			_StartService(struct service& service);
			status_t			_StopService(struct service* service);
			status_t			_ToService(const BMessage& message,
									struct service*& service);
			void				_Update(const BMessage& services);
			status_t			_LaunchService(struct service& service,
									int socket);
			status_t			_Listener();
	static	status_t			_Listener(void* self);

private:
			thread_id			fListener; /**< Listener thread ID */
			BLocker				fLock; /**< Protects socket and name maps */
			ServiceNameMap		fNameMap; /**< Map of service name to service struct */
			ServiceSocketMap	fSocketMap; /**< Map of socket fd to service connection */
			uint32				fUpdate; /**< Monotonic update counter */
			int					fReadPipe;
			int					fWritePipe;
			int					fMinSocket;
			int					fMaxSocket;
			fd_set				fSet; /**< Active file descriptor set for select() */
};


const static uint32 kMsgUpdateServices = 'srvU';


#endif	// SERVICES_H
