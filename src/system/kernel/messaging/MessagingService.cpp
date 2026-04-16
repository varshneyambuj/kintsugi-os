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
 *   Copyright 2005-2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MessagingService.cpp
 * @brief Kernel side of the registrar messaging service.
 *
 * The messaging service is a singleton constructed in a static buffer by
 * init_messaging_service() and lives for the life of the kernel. The
 * userland registrar registers itself via _user_register_messaging_service()
 * and provides a (lock, counter) semaphore pair; the kernel then allocates
 * shared MessagingArea ring buffers and streams command records into them.
 *
 * The send path is send_message() -> MessagingService::SendMessage() ->
 * MessagingService::_AllocateCommand() -> MessagingArea::AllocateCommand().
 * Commands are written into the last (active) area; if it fills up, a new
 * area is created (or a previously drained one recycled) and linked in
 * via MessagingArea::SetNextArea(). When the area was previously empty,
 * CommitCommand() wakes the registrar by releasing the counter semaphore.
 *
 * Locking: the service as a whole is protected by a recursive_lock; each
 * MessagingArea is independently protected by a benaphore built on its
 * header's lock_counter plus the registrar's lock semaphore.
 */


//! kernel-side implementation of the messaging service


#include <new>

#include <AutoDeleter.h>
#include <BytePointer.h>
#include <KernelExport.h>
#include <KMessage.h>
#include <messaging.h>
#include <MessagingServiceDefs.h>

#include "MessagingService.h"

//#define TRACE_MESSAGING_SERVICE
#ifdef TRACE_MESSAGING_SERVICE
#	define PRINT(x) dprintf x
#else
#	define PRINT(x) ;
#endif


using namespace std;

static MessagingService *sMessagingService = NULL;

static const int32 kMessagingAreaSize = B_PAGE_SIZE * 4;


// #pragma mark - MessagingArea


/**
 * @brief Default-constructs an uninitialised MessagingArea.
 *
 * Fields are populated by Create(); this constructor is only used through
 * placement-/heap-allocation by the factory.
 */
MessagingArea::MessagingArea()
{
}


/**
 * @brief Destroys the area and releases its shared memory.
 *
 * Deletes the kernel area identified by @c fID if it was successfully
 * created.
 */
MessagingArea::~MessagingArea()
{
	if (fID >= 0)
		delete_area(fID);
}


/**
 * @brief Factory that creates a fully initialised MessagingArea.
 *
 * Allocates a new area of @c kMessagingAreaSize bytes, wires up the
 * (lock, counter) semaphore pair shared with the registrar, and stamps a
 * fresh header into the shared memory.
 *
 * @param lockSem    Semaphore used as the area's lock fall-back.
 * @param counterSem Semaphore released when a command is pushed onto an
 *                   empty area.
 * @return Newly created area, or NULL on allocation failure.
 */
MessagingArea *
MessagingArea::Create(sem_id lockSem, sem_id counterSem)
{
	// allocate the object on the heap
	MessagingArea *area = new(nothrow) MessagingArea;
	if (!area)
		return NULL;

	// create the area
	area->fID = create_area("messaging", (void**)&area->fHeader,
		B_ANY_KERNEL_ADDRESS, kMessagingAreaSize, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (area->fID < 0) {
		delete area;
		return NULL;
	}

	// finish the initialization of the object
	area->fSize = kMessagingAreaSize;
	area->fLockSem = lockSem;
	area->fCounterSem = counterSem;
	area->fNextArea = NULL;
	area->InitHeader();

	return area;
}


/**
 * @brief (Re)initialises the shared area header.
 *
 * Sets the benaphore counter to locked (1), records the area's size and
 * id, links to the next area, and marks the ring buffer as empty.
 */
void
MessagingArea::InitHeader()
{
	fHeader->lock_counter = 1;			// create locked
	fHeader->size = fSize;
	fHeader->kernel_area = fID;
	fHeader->next_kernel_area = (fNextArea ? fNextArea->ID() : -1);
	fHeader->command_count = 0;
	fHeader->first_command = 0;
	fHeader->last_command = 0;
}


/**
 * @brief Reports whether a command with @a dataSize bytes of payload can
 *        ever fit in an area.
 *
 * @param dataSize Payload size in bytes.
 * @return true if a messaging_command plus @a dataSize fits within a
 *         single area after the fixed header.
 */
bool
MessagingArea::CheckCommandSize(int32 dataSize)
{
	int32 size = sizeof(messaging_command) + dataSize;

	return (dataSize >= 0
		&& size <= kMessagingAreaSize - (int32)sizeof(messaging_area_header));
}


/**
 * @brief Acquires the area using its benaphore.
 *
 * Fast path: an atomic increment of the shared @c lock_counter from zero
 * succeeds uncontended. Otherwise falls back to acquiring the registrar's
 * lock semaphore.
 *
 * @return true on success, false if the lock semaphore could not be
 *         acquired.
 */
bool
MessagingArea::Lock()
{
	// benaphore-like locking
	if (atomic_add(&fHeader->lock_counter, 1) == 0)
		return true;

	return (acquire_sem(fLockSem) == B_OK);
}


/**
 * @brief Releases the area, waking a waiter when contended.
 *
 * Pairs with Lock(); releases the lock semaphore iff the benaphore
 * counter indicates contention.
 */
void
MessagingArea::Unlock()
{
	if (atomic_add(&fHeader->lock_counter, -1) > 1)
		release_sem(fLockSem);
}


/**
 * @brief Returns the kernel area id.
 *
 * @return Area id assigned by create_area().
 */
area_id
MessagingArea::ID() const
{
	return fID;
}


/**
 * @brief Returns the area's total byte size.
 *
 * @return Size of the shared area in bytes.
 */
int32
MessagingArea::Size() const
{
	return fSize;
}


/**
 * @brief Reports whether the ring buffer currently holds no commands.
 *
 * @return true if no pending commands remain.
 */
bool
MessagingArea::IsEmpty() const
{
	return fHeader->command_count == 0;
}


/**
 * @brief Reserves space for a new command in the area's ring buffer.
 *
 * If the area is empty, the command is placed at the start of the ring;
 * otherwise the method tries to append after the current last command and
 * wraps back to the start when there is not enough room at the tail.
 *
 * @param commandWhat Command identifier recorded in the header.
 * @param dataSize    Payload size in bytes.
 * @param wasEmpty    Out: true if the area had no commands prior to this
 *                   reservation; used by the caller to decide whether to
 *                   signal the counter semaphore.
 * @return Pointer to the command's payload area, or NULL if there is not
 *         enough free space.
 */
void *
MessagingArea::AllocateCommand(uint32 commandWhat, int32 dataSize,
	bool &wasEmpty)
{
	int32 size = sizeof(messaging_command) + dataSize;

	if (dataSize < 0 || size > fSize - (int32)sizeof(messaging_area_header))
		return NULL;

	// the area is used as a ring buffer
	int32 startOffset = sizeof(messaging_area_header);

	// the simple case first: the area is empty
	int32 commandOffset;
	wasEmpty = (fHeader->command_count == 0);
	if (wasEmpty) {
		commandOffset = startOffset;

		// update the header
		fHeader->command_count++;
		fHeader->first_command = fHeader->last_command = commandOffset;
	} else {
		int32 firstCommandOffset = fHeader->first_command;
		int32 lastCommandOffset = fHeader->last_command;
		int32 firstCommandSize;
		int32 lastCommandSize;
		messaging_command *firstCommand = _CheckCommand(firstCommandOffset,
			firstCommandSize);
		messaging_command *lastCommand = _CheckCommand(lastCommandOffset,
			lastCommandSize);
		if (!firstCommand || !lastCommand) {
			// something has been screwed up
			return NULL;
		}

		// find space for the command
		if (firstCommandOffset <= lastCommandOffset) {
			// not wrapped
			// try to allocate after the last command
			if (size <= fSize - (lastCommandOffset + lastCommandSize)) {
				commandOffset = (lastCommandOffset + lastCommandSize);
			} else {
				// is there enough space before the first command?
				if (size > firstCommandOffset - startOffset)
					return NULL;
				commandOffset = startOffset;
			}
		} else {
			// wrapped: we can only allocate between the last and the first
			// command
			commandOffset = lastCommandOffset + lastCommandSize;
			if (size > firstCommandOffset - commandOffset)
				return NULL;
		}

		// update the header and the last command
		fHeader->command_count++;
		lastCommand->next_command = fHeader->last_command = commandOffset;
	}

	// init the command
	BytePointer<messaging_command> command(fHeader);
	command += commandOffset;
	command->next_command = 0;
	command->command = commandWhat;
	command->size = size;

	return command->data;
}


/**
 * @brief Notifies the registrar that a previously empty area now has work.
 *
 * Releases the counter semaphore so the userland consumer wakes up.
 */
void
MessagingArea::CommitCommand()
{
	// TODO: If invoked while locked, we should supply B_DO_NOT_RESCHEDULE.
	release_sem(fCounterSem);
}


/**
 * @brief Links this area to a successor area and updates the shared
 *        header accordingly.
 *
 * @param area New next-area pointer, or NULL to unlink.
 */
void
MessagingArea::SetNextArea(MessagingArea *area)
{
	fNextArea = area;
	fHeader->next_kernel_area = (fNextArea ? fNextArea->ID() : -1);
}


/**
 * @brief Returns the next area in the service's linked list.
 *
 * @return Successor MessagingArea, or NULL if this is the last area.
 */
MessagingArea *
MessagingArea::NextArea() const
{
	return fNextArea;
}


/**
 * @brief Validates a command offset within the ring and returns its size.
 *
 * Checks alignment and bounds of the offset and the declared command
 * size, rounding the size up to the 4-byte alignment that the layout
 * requires.
 *
 * @param offset Byte offset within the area.
 * @param size   Out: aligned command size in bytes.
 * @return Pointer to the command at @a offset, or NULL if the offset or
 *         size is malformed.
 */
messaging_command *
MessagingArea::_CheckCommand(int32 offset, int32 &size)
{
	// check offset
	if (offset < (int32)sizeof(messaging_area_header)
		|| offset + (int32)sizeof(messaging_command) > fSize
		|| (offset & 0x3)) {
		return NULL;
	}

	// get and check size
	BytePointer<messaging_command> command(fHeader);
	command += offset;
	size = command->size;
	if (size < (int32)sizeof(messaging_command))
		return NULL;
	size = (size + 3) & ~0x3;	// align
	if (offset + size > fSize)
		return NULL;

	return &command;
}


// #pragma mark - MessagingService


/**
 * @brief Constructs the singleton messaging service.
 *
 * Initialises the recursive lock that serialises access to the area list.
 * In practice the only instance is placement-new'd by
 * init_messaging_service().
 */
MessagingService::MessagingService()
	:
	fFirstArea(NULL),
	fLastArea(NULL)
{
	recursive_lock_init(&fLock, "messaging service");
}


/**
 * @brief Destructor; never expected to run.
 *
 * The service is designed to live for the kernel's lifetime, so this
 * destructor only appears on initialisation failure.
 */
MessagingService::~MessagingService()
{
	// Should actually never be called. Once created the service stays till the
	// bitter end.
}


/**
 * @brief Reports post-construction initialisation status.
 *
 * @return Always B_OK; present for future compatibility.
 */
status_t
MessagingService::InitCheck() const
{
	return B_OK;
}


/**
 * @brief Acquires the service-wide recursive lock.
 *
 * @return true on success.
 */
bool
MessagingService::Lock()
{
	return recursive_lock_lock(&fLock) == B_OK;
}


/**
 * @brief Releases the service-wide recursive lock.
 */
void
MessagingService::Unlock()
{
	recursive_lock_unlock(&fLock);
}


/**
 * @brief Registers the calling team as the userland messaging server.
 *
 * Validates that the supplied semaphores belong to the caller's team,
 * creates the first shared MessagingArea, and records the caller as the
 * server team. Only one service may be registered at a time.
 *
 * @param lockSem    Semaphore used to lock individual MessagingAreas.
 * @param counterSem Semaphore released when a command is pushed onto an
 *                   empty area.
 * @param areaID     Out: id of the kernel area that backs the first
 *                   shared region.
 * @return B_OK on success; B_BAD_VALUE if a server is already registered
 *         or the semaphores are invalid; B_NO_MEMORY if no area can be
 *         allocated.
 */
status_t
MessagingService::RegisterService(sem_id lockSem, sem_id counterSem,
	area_id &areaID)
{
	// check, if a service is already registered
	if (fFirstArea)
		return B_BAD_VALUE;

	status_t error = B_OK;

	// check, if the semaphores are valid and belong to the calling team
	thread_info threadInfo;
	error = get_thread_info(find_thread(NULL), &threadInfo);

	sem_info lockSemInfo;
	if (error == B_OK)
		error = get_sem_info(lockSem, &lockSemInfo);

	sem_info counterSemInfo;
	if (error == B_OK)
		error = get_sem_info(counterSem, &counterSemInfo);

	if (error != B_OK)
		return error;

	if (threadInfo.team != lockSemInfo.team
		|| threadInfo.team != counterSemInfo.team) {
		return B_BAD_VALUE;
	}

	// create an area
	fFirstArea = fLastArea = MessagingArea::Create(lockSem, counterSem);
	if (!fFirstArea)
		return B_NO_MEMORY;

	areaID = fFirstArea->ID();
	fFirstArea->Unlock();

	// store the server team and the semaphores
	fServerTeam = threadInfo.team;
	fLockSem = lockSem;
	fCounterSem = counterSem;

	return B_OK;
}


/**
 * @brief Tears down the registration established by RegisterService().
 *
 * Requires the caller to be the server team. Destroys every area in the
 * list and clears the recorded server identity.
 *
 * @return B_OK on success, B_BAD_VALUE if the caller is not the server
 *         team, or an error from get_thread_info().
 */
status_t
MessagingService::UnregisterService()
{
	// check, if the team calling this function is indeed the server team
	thread_info threadInfo;
	status_t error = get_thread_info(find_thread(NULL), &threadInfo);
	if (error != B_OK)
		return error;

	if (threadInfo.team != fServerTeam)
		return B_BAD_VALUE;

	// delete all areas
	while (fFirstArea) {
		MessagingArea *area = fFirstArea;
		fFirstArea = area->NextArea();
		delete area;
	}
	fLastArea = NULL;

	// unset the other members
	fLockSem = -1;
	fCounterSem = -1;
	fServerTeam = -1;

	return B_OK;
}


/**
 * @brief Enqueues a MESSAGING_COMMAND_SEND_MESSAGE command for the
 *        registrar.
 *
 * Builds the command record (target list followed by the raw message
 * bytes) inside an allocated region of the active MessagingArea,
 * releases that area's lock, and signals the counter semaphore when the
 * area had previously been empty.
 *
 * @param message     Pointer to the message payload.
 * @param messageSize Payload size in bytes.
 * @param targets     Array of delivery targets.
 * @param targetCount Number of entries in @a targets.
 * @return B_OK on success, B_BAD_VALUE for illegal arguments, or an
 *         error from _AllocateCommand().
 */
status_t
MessagingService::SendMessage(const void *message, int32 messageSize,
	const messaging_target *targets, int32 targetCount)
{
PRINT(("MessagingService::SendMessage(%p, %ld, %p, %ld)\n", message,
messageSize, targets, targetCount));
	if (!message || messageSize <= 0 || !targets || targetCount <= 0)
		return B_BAD_VALUE;

	int32 dataSize = sizeof(messaging_command_send_message)
		+ targetCount * sizeof(messaging_target) + messageSize;

	// allocate space for the command
	MessagingArea *area;
	void *data;
	bool wasEmpty;
	status_t error = _AllocateCommand(MESSAGING_COMMAND_SEND_MESSAGE, dataSize,
		area, data, wasEmpty);
	if (error != B_OK) {
		PRINT(("MessagingService::SendMessage(): Failed to allocate space for "
			"send message command.\n"));
		return error;
	}
PRINT(("  Allocated space for send message command: area: %p, data: %p, "
"wasEmpty: %d\n", area, data, wasEmpty));

	// prepare the command
	messaging_command_send_message *command
		= (messaging_command_send_message*)data;
	command->message_size = messageSize;
	command->target_count = targetCount;
	memcpy(command->targets, targets, sizeof(messaging_target) * targetCount);
	memcpy((char*)command + (dataSize - messageSize), message, messageSize);

	// shoot
	area->Unlock();
	if (wasEmpty)
		area->CommitCommand();

	return B_OK;
}


/**
 * @brief Reserves space for a command in the active area, growing the
 *        area list if necessary.
 *
 * Walks the front of the area list, discarding drained areas (keeping at
 * most one for recycling). If the current last area cannot fit the
 * command, either the recycled area is rewound or a freshly created area
 * is appended, and the command is allocated there. On exit the chosen
 * area is still held locked; the caller is responsible for unlocking and
 * for committing when @a wasEmpty.
 *
 * @param commandWhat Command id passed through to AllocateCommand().
 * @param size        Payload size in bytes.
 * @param area        Out: area in which space was reserved (still locked).
 * @param data        Out: pointer to the command payload.
 * @param wasEmpty    Out: whether the area had been empty before.
 * @return B_OK on success, B_NO_INIT when no service is registered,
 *         B_BAD_VALUE if the command is too large, or B_NO_MEMORY if a
 *         new area cannot be created.
 */
status_t
MessagingService::_AllocateCommand(int32 commandWhat, int32 size,
	MessagingArea *&area, void *&data, bool &wasEmpty)
{
	if (!fFirstArea)
		return B_NO_INIT;

	if (!MessagingArea::CheckCommandSize(size))
		return B_BAD_VALUE;

	// delete the discarded areas (save one)
	ObjectDeleter<MessagingArea> discardedAreaDeleter;
	MessagingArea *discardedArea = NULL;

	while (fFirstArea != fLastArea) {
		area = fFirstArea;
		area->Lock();
		if (!area->IsEmpty()) {
			area->Unlock();
			break;
		}

		PRINT(("MessagingService::_AllocateCommand(): Discarding area: %p\n",
			area));

		fFirstArea = area->NextArea();
		area->SetNextArea(NULL);
		discardedArea = area;
		discardedAreaDeleter.SetTo(area);
	}

	// allocate space for the command in the last area
	area = fLastArea;
	area->Lock();
	data = area->AllocateCommand(commandWhat, size, wasEmpty);

	if (!data) {
		// not enough space in the last area: create a new area or reuse a
		// discarded one
		if (discardedArea) {
			area = discardedAreaDeleter.Detach();
			area->InitHeader();
			PRINT(("MessagingService::_AllocateCommand(): Not enough space "
				"left in current area. Recycling discarded one: %p\n", area));
		} else {
			area = MessagingArea::Create(fLockSem, fCounterSem);
			PRINT(("MessagingService::_AllocateCommand(): Not enough space "
				"left in current area. Allocated new one: %p\n", area));
		}
		if (!area) {
			fLastArea->Unlock();
			return B_NO_MEMORY;
		}

		// add the new area
		fLastArea->SetNextArea(area);
		fLastArea->Unlock();
		fLastArea = area;

		// allocate space for the command
		data = area->AllocateCommand(commandWhat, size, wasEmpty);

		if (!data) {
			// that should never happen
			area->Unlock();
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


// #pragma mark - kernel private


/**
 * @brief Kernel-private entry point: send a raw message to targets.
 *
 * Locks the singleton service and forwards to
 * MessagingService::SendMessage().
 *
 * @param message     Pointer to the raw message bytes.
 * @param messageSize Size in bytes.
 * @param targets     Array of delivery targets.
 * @param targetCount Number of entries in @a targets.
 * @return B_NO_INIT if the service has not been initialised yet,
 *         B_BAD_VALUE if the service lock cannot be acquired, or the
 *         status reported by SendMessage().
 */
status_t
send_message(const void *message, int32 messageSize,
	const messaging_target *targets, int32 targetCount)
{
	// check, if init_messaging_service() has been called yet
	if (!sMessagingService)
		return B_NO_INIT;

	if (!sMessagingService->Lock())
		return B_BAD_VALUE;

	status_t error = sMessagingService->SendMessage(message, messageSize,
		targets, targetCount);

	sMessagingService->Unlock();

	return error;
}


/**
 * @brief Kernel-private entry point: send a KMessage to targets.
 *
 * Thin wrapper around the raw send_message() using the KMessage's flat
 * buffer as the payload.
 *
 * @param message     KMessage to deliver; must not be NULL.
 * @param targets     Array of delivery targets.
 * @param targetCount Number of entries in @a targets.
 * @return B_BAD_VALUE if @a message is NULL, otherwise the status of the
 *         underlying raw send.
 */
status_t
send_message(const KMessage *message, const messaging_target *targets,
	int32 targetCount)
{
	if (!message)
		return B_BAD_VALUE;

	return send_message(message->Buffer(), message->ContentSize(), targets,
		targetCount);
}


/**
 * @brief Constructs the singleton messaging service at boot time.
 *
 * Placement-new's a MessagingService instance into a static buffer so the
 * service survives for the kernel's lifetime. On InitCheck() failure the
 * instance is destroyed and the global pointer cleared.
 *
 * @return B_OK on success, or the InitCheck() error.
 */
status_t
init_messaging_service()
{
	static char buffer[sizeof(MessagingService)];

	if (!sMessagingService)
		sMessagingService = new(buffer) MessagingService;

	status_t error = sMessagingService->InitCheck();

	// cleanup on error
	if (error != B_OK) {
		dprintf("ERROR: Failed to init messaging service: %s\n",
			strerror(error));
		sMessagingService->~MessagingService();
		sMessagingService = NULL;
	}

	return error;
}


// #pragma mark - syscalls


/**
 * @brief Syscall: registers the calling userland server as the messaging
 *        service for the kernel.
 *
 * The semaphore pair must belong to the caller's team and both counters
 * must have been initialised to 0: @a lockSem is used as a benaphore
 * fall-back for per-area locking, and @a counterSem is released every
 * time the kernel pushes a command into a previously empty area.
 *
 * @param lockSem    Locking semaphore (counter initialised to 0).
 * @param counterSem Work-signal semaphore (counter initialised to 0).
 * @return The id of the kernel area used for communication on success,
 *         or a negative error code.
 */
area_id
_user_register_messaging_service(sem_id lockSem, sem_id counterSem)
{
	// check, if init_messaging_service() has been called yet
	if (!sMessagingService)
		return B_NO_INIT;

	if (!sMessagingService->Lock())
		return B_BAD_VALUE;

	area_id areaID = 0;
	status_t error = sMessagingService->RegisterService(lockSem, counterSem,
		areaID);

	sMessagingService->Unlock();

	return (error != B_OK ? error : areaID);
}


/**
 * @brief Syscall: unregisters the caller as the messaging service.
 *
 * Locks the service, delegates to MessagingService::UnregisterService(),
 * and unlocks on exit.
 *
 * @return B_NO_INIT if no service exists; B_BAD_VALUE if the lock cannot
 *         be acquired; otherwise the status from UnregisterService().
 */
status_t
_user_unregister_messaging_service()
{
	// check, if init_messaging_service() has been called yet
	if (!sMessagingService)
		return B_NO_INIT;

	if (!sMessagingService->Lock())
		return B_BAD_VALUE;

	status_t error = sMessagingService->UnregisterService();

	sMessagingService->Unlock();

	return error;
}
