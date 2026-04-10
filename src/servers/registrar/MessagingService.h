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
   Copyright 2005, Ingo Weinhold, bonefish@users.sf.net. All rights reserved.
   Distributed under the terms of the MIT License.
 */

/** @file MessagingService.h
 *  @brief Bridges kernel-side messaging areas with registrar command handlers. */
#ifndef MESSAGING_SERVICE_H
#define MESSAGING_SERVICE_H

#include <Locker.h>
#include <MessagingServiceDefs.h>

// MessagingCommandHandler
/** @brief Abstract handler for commands received from the kernel messaging area. */
class MessagingCommandHandler {
public:
	MessagingCommandHandler();
	virtual ~MessagingCommandHandler();

	/** @brief Processes a command received from the kernel messaging area. */
	virtual void HandleMessagingCommand(uint32 command, const void *data,
		int32 dataSize) = 0;
};

// MessagingArea
/** @brief Maps a shared kernel area and pops messaging commands from it. */
class MessagingArea {
public:
	~MessagingArea();

	/** @brief Creates a MessagingArea from a kernel area ID and synchronization primitives. */
	static status_t Create(area_id kernelAreaID, sem_id lockSem,
		sem_id counterSem, MessagingArea *&area);

	/** @brief Acquires the area or service lock. */
	bool Lock();
	/** @brief Releases the area or service lock. */
	void Unlock();

	/** @brief Returns the area_id of this messaging area. */
	area_id ID() const;
	/** @brief Returns the size in bytes of this messaging area. */
	int32 Size() const;

	/** @brief Returns the number of pending commands in the area. */
	int32 CountCommands() const;
	/** @brief Removes and returns the next command from the area. */
	const messaging_command *PopCommand();

	/** @brief Releases resources associated with this messaging area. */
	void Discard();

	/** @brief Returns the kernel area ID of the next chained messaging area. */
	area_id NextKernelAreaID() const;
	/** @brief Sets the pointer to the next MessagingArea in the chain. */
	void SetNextArea(MessagingArea *area);
	/** @brief Returns the next MessagingArea in the chain. */
	MessagingArea *NextArea() const;

private:
	MessagingArea();

	messaging_area_header	*fHeader;
	area_id					fID;
	int32					fSize;
	sem_id					fLockSem;
	sem_id					fCounterSem;	// TODO: Remove, if not needed.
	MessagingArea			*fNextArea;
};

// MessagingService
/** @brief Singleton that reads commands from kernel messaging areas and dispatches them to handlers. */
class MessagingService {
private:
	MessagingService();
	~MessagingService();

	status_t Init();

public:
	/** @brief Creates the singleton MessagingService instance. */
	static status_t CreateDefault();
	/** @brief Destroys the singleton MessagingService instance. */
	static void DeleteDefault();
	/** @brief Returns the singleton MessagingService instance. */
	static MessagingService *Default();

	/** @brief Registers a handler for a specific command code. */
	void SetCommandHandler(uint32 command, MessagingCommandHandler *handler);

private:
	MessagingCommandHandler *_GetCommandHandler(uint32 command) const;

	static int32 _CommandProcessorEntry(void *data);
	int32 _CommandProcessor();

	class DefaultSendCommandHandler;
	struct CommandHandlerMap;

	static MessagingService	*sService;

	mutable BLocker			fLock;
	sem_id					fLockSem;
	sem_id					fCounterSem;
	MessagingArea			*fFirstArea;
	CommandHandlerMap		*fCommandHandlers;
	thread_id				fCommandProcessor;
	volatile bool			fTerminating;
};

#endif	// MESSAGING_SERVICE_H
