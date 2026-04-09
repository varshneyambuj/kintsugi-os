/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2015, Haiku, Inc.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Julian Harnath, <julian.harnath@rwth-aachen.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file TestServerLoopAdapter.h
 *  @brief Adapter that runs a MessageLooper as a BApplication lookalike for testing. */

#ifndef TEST_SERVER_LOOP_ADAPTER_H
#define TEST_SERVER_LOOP_ADAPTER_H

#include "MessageLooper.h"


class BMessage;
class Desktop;


/** @brief Bridges the MessageLooper protocol to a BApplication-style interface for unit tests. */
class TestServerLoopAdapter : public MessageLooper {
public:
	/** @brief Constructs the adapter and starts the server loop.
	 *  @param signature  Application MIME signature.
	 *  @param looperName Name for the internal looper thread.
	 *  @param port       Pre-existing message port to use, or 0 to create one.
	 *  @param initGui    If true, initialise GUI subsystems.
	 *  @param outError   Receives B_OK or an initialisation error code. */
								TestServerLoopAdapter(const char* signature,
									const char* looperName, port_id port,
									bool initGui, status_t* outError);
	virtual						~TestServerLoopAdapter();

	// MessageLooper interface
	/** @brief Returns the message port this adapter listens on.
	 *  @return Message port ID. */
	virtual	port_id				MessagePort() const { return fMessagePort; }

	/** @brief Starts the message-processing loop.
	 *  @return B_OK on success, an error code otherwise. */
	virtual	status_t			Run();

	// BApplication interface
	/** @brief Override to handle application-level messages.
	 *  @param message Incoming BMessage. */
	virtual	void				MessageReceived(BMessage* message) = 0;

	/** @brief Override to handle quit requests; default returns true.
	 *  @return true to allow quitting. */
	virtual	bool				QuitRequested() { return true; }

private:
	// MessageLooper interface
	/** @brief Dispatches a decoded protocol message to subclass logic.
	 *  @param code Protocol message code.
	 *  @param link Receiver to read parameters from. */
	virtual	void				_DispatchMessage(int32 code,
									BPrivate::LinkReceiver &link);

	/** @brief Finds the Desktop for the given user and screen target.
	 *  @param userID       UID of the requesting user.
	 *  @param targetScreen Name of the target screen.
	 *  @return Pointer to the Desktop, or NULL. */
	virtual	Desktop*			_FindDesktop(uid_t userID,
									const char* targetScreen) = 0;

	/** @brief Creates and returns a new message port for this adapter.
	 *  @return Newly created port_id. */
			port_id				_CreatePort();



private:
			port_id				fMessagePort;
};


#endif // TEST_SERVER_LOOP_ADAPTER_H
