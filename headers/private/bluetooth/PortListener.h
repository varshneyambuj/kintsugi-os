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
 *   Copyright 2009 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file PortListener.h
 *  @brief Generic, template-based Haiku port listener that spawns a dedicated
 *         thread to dispatch typed messages to a user-supplied callback. */

#ifndef PORTLISTENER_H_
#define PORTLISTENER_H_

#include <OS.h>

/** @brief Template class that wraps a Haiku port and a companion thread to
 *         receive typed messages and dispatch them to a callback function.
 *
 *  @tparam TYPE             The C++ type whose instances are transferred through
 *                           the port; messages are cast to TYPE* before being
 *                           passed to the handler.
 *  @tparam MAX_MESSAGE_SIZE Maximum byte size of a single port message (default 256).
 *  @tparam MAX_MESSAGE_DEEP Capacity of the port's message queue (default 16).
 *  @tparam PRIORITY         Scheduling priority of the listener thread
 *                           (default B_URGENT_DISPLAY_PRIORITY). */
template <
	typename TYPE,
	ssize_t MAX_MESSAGE_SIZE = 256,
	size_t MAX_MESSAGE_DEEP	= 16,
	uint32 PRIORITY	= B_URGENT_DISPLAY_PRIORITY>
class PortListener {
public:
	/** @brief Function pointer type for the message handler callback.
	 *
	 *  @param buffer Pointer to the received message data, cast to TYPE*.
	 *  @param code   The 32-bit message code written to the port.
	 *  @param size   Byte count of the received message payload.
	 *  @return B_OK to continue processing messages; any other value causes
	 *          the listener thread to terminate. */
	typedef status_t (*port_listener_func)(TYPE*, int32, size_t);

	/** @brief Constructs a PortListener, creating or finding the named port
	 *         and preparing (but not starting) the listener thread.
	 *  @param name    Name for both the port and (with " thread" appended) the
	 *                 listener thread.
	 *  @param handler Callback invoked for each message received on the port. */
	PortListener(const char* name, port_listener_func handler)
	{
		fInformation.func = handler;
		fInformation.port = &fPort;

		fPortName = strdup(name);

		fThreadName = (char*)malloc(strlen(name) + strlen(" thread") + 1);
		fThreadName = strcpy(fThreadName, fPortName);
		fThreadName = strcat(fThreadName, " thread");

		InitCheck();
	}


	/** @brief Destroys the PortListener, closing the port and waiting for the
	 *         listener thread to finish before freeing resources. */
	~PortListener()
	{
		status_t status;

		close_port(fPort);
		// Closing the port	should provoke the thread to finish
		wait_for_thread(fThread, &status);

		free(fThreadName);
		free(fPortName);
	}


	/** @brief Sends a code-only message with no payload to the port.
	 *  @param code The 32-bit message code to write.
	 *  @return B_OK on success, or a system error code. */
	status_t Trigger(int32 code)
	{
			return write_port(fPort, code, NULL, 0);
	}


	/** @brief Sends a typed message with a payload buffer to the port.
	 *  @param code   The 32-bit message code to write.
	 *  @param buffer Pointer to the payload data; must not be NULL.
	 *  @param size   Byte length of @p buffer.
	 *  @return B_OK on success, B_ERROR if @p buffer is NULL, or a system
	 *          error code. */
	status_t Trigger(int32 code, TYPE* buffer, size_t size)
	{
		if (buffer == NULL)
			return B_ERROR;

		return write_port(fPort, code, buffer, size);
	}


	/** @brief Verifies that the port and listener thread exist, creating them
	 *         if necessary; safe to call multiple times.
	 *  @return B_OK if both the port and thread are ready, or a system error
	 *          code if either could not be created. */
	status_t InitCheck()
	{
		// Create Port
		fPort = find_port(fPortName);
		if (fPort == B_NAME_NOT_FOUND) {
			fPort = create_port(MAX_MESSAGE_DEEP, fPortName);
		}

		if (fPort < B_OK)
			return fPort;

		#ifdef KERNEL_LAND
		// if this is the case you better stay with kernel
		set_port_owner(fPort, B_SYSTEM_TEAM);
		#endif

		// Create Thread
		fThread = find_thread(fThreadName);
		if (fThread < B_OK) {
#ifdef KERNEL_LAND
			fThread = spawn_kernel_thread((thread_func)&PortListener<TYPE,
				MAX_MESSAGE_SIZE, MAX_MESSAGE_DEEP, PRIORITY>::threadFunction,
				fThreadName, PRIORITY, &fInformation);
#else
			fThread = spawn_thread((thread_func)&PortListener<TYPE,
				MAX_MESSAGE_SIZE, MAX_MESSAGE_DEEP, PRIORITY>::threadFunction,
				fThreadName, PRIORITY, &fInformation);
#endif
		}

		if (fThread < B_OK)
			return fThread;

		return B_OK;
	}


	/** @brief Verifies the port and thread are ready, then resumes the listener
	 *         thread so it begins processing incoming messages.
	 *  @return B_OK on success, or a system error code if InitCheck() failed or
	 *          resume_thread() failed. */
	status_t Launch()
	{
		status_t check = InitCheck();

		if (check < B_OK)
			return check;

		return resume_thread(fThread);
	}


	/** @brief Closes the port, causing the listener thread to exit, then
	 *         waits for the thread to finish.
	 *  @return The exit status of the listener thread. */
	status_t Stop()
	{
		status_t status;

		close_port(fPort);
		// Closing the port	should provoke the thread to finish
		wait_for_thread(fThread, &status);

		return status;
	}


private:

	/** @brief Internal structure passed to the listener thread, bundling the
	 *         port ID reference and the user-supplied handler callback. */
	struct PortListenerInfo {
		port_id* port;
		port_listener_func func;
	} fInformation;

	port_id fPort;
	thread_id fThread;
	char* fThreadName;
	char* fPortName;

	/** @brief Entry point for the listener thread; reads messages from the port
	 *         in a loop and dispatches each to the registered handler.
	 *  @param data Pointer to the PortListenerInfo for this instance.
	 *  @return B_BAD_PORT_ID if the port was closed externally, or the non-B_OK
	 *          status returned by the handler callback. */
	static int32 threadFunction(void* data)
	{
		ssize_t	ssizePort;
		ssize_t	ssizeRead;
		status_t status = B_OK;
		int32 code;

		port_id* port = ((struct PortListenerInfo*)data)->port;
		port_listener_func handler = ((struct PortListenerInfo*)data)->func;


		TYPE* buffer = (TYPE*)malloc(MAX_MESSAGE_SIZE);

		while ((ssizePort = port_buffer_size(*port)) != B_BAD_PORT_ID) {

			if (ssizePort <= 0) {
				snooze(500 * 1000);
				continue;
			}

			if (ssizePort >	MAX_MESSAGE_SIZE) {
				snooze(500 * 1000);
				continue;
			}

			ssizeRead = read_port(*port, &code, (void*)buffer, ssizePort);

			if (ssizeRead != ssizePort)
				continue;

			status = handler(buffer, code,	ssizePort);

			if (status != B_OK)
				break;

		}

		#ifdef DEBUG_PORTLISTENER
		#ifdef KERNEL_LAND
		dprintf("Error in PortListener handler=%s port=%s\n", strerror(status),
			strerror(ssizePort));
		#else
		printf("Error in PortListener handler=%s port=%s\n", strerror(status),
			strerror(ssizePort));
		#endif
		#endif

		free(buffer);

		if (ssizePort == B_BAD_PORT_ID) // the port disappeared
			return ssizePort;

		return status;
	}

}; // PortListener

#endif // PORTLISTENER_H_
