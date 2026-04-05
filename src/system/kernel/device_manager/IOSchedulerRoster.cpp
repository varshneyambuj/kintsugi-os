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
 *   Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2024, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file IOSchedulerRoster.cpp
 * @brief Global registry of active IOScheduler instances, used for statistics and debugging.
 */


#include "IOSchedulerRoster.h"

#include <util/AutoLock.h>


/*static*/ IOSchedulerRoster IOSchedulerRoster::sDefaultInstance;


/**
 * @brief Constructs the roster, initialising its mutex and notification service.
 *
 * Sets fNextID to 1, initialises the "I/O" DefaultNotificationService, and
 * registers the service so subscribers can begin receiving events.
 */
IOSchedulerRoster::IOSchedulerRoster()
	:
	fNextID(1),
	fNotificationService("I/O")
{
	mutex_init(&fLock, "IOSchedulerRoster");
	fNotificationService.Register();
}


/**
 * @brief Destroys the roster, releasing the mutex and unregistering the
 *        notification service.
 */
IOSchedulerRoster::~IOSchedulerRoster()
{
	mutex_destroy(&fLock);
	fNotificationService.Unregister();
}


/**
 * @brief Registers a scheduler with the roster and broadcasts an
 *        IO_SCHEDULER_ADDED event to all listeners.
 *
 * The scheduler is appended to fSchedulers under fLock, then the lock is
 * released before the notification is sent to avoid holding it during
 * potentially blocking listener callbacks.
 *
 * @param scheduler  The IOScheduler instance to add.
 */
void
IOSchedulerRoster::AddScheduler(IOScheduler* scheduler)
{
	AutoLocker<IOSchedulerRoster> locker(this);
	fSchedulers.Add(scheduler);
	locker.Unlock();

	Notify(IO_SCHEDULER_ADDED, scheduler);
}


/**
 * @brief Unregisters a scheduler from the roster and broadcasts an
 *        IO_SCHEDULER_REMOVED event to all listeners.
 *
 * The scheduler is removed from fSchedulers under fLock, then the lock is
 * released before the notification is sent.
 *
 * @param scheduler  The IOScheduler instance to remove.
 */
void
IOSchedulerRoster::RemoveScheduler(IOScheduler* scheduler)
{
	AutoLocker<IOSchedulerRoster> locker(this);
	fSchedulers.Remove(scheduler);
	locker.Unlock();

	Notify(IO_SCHEDULER_REMOVED, scheduler);
}


/**
 * @brief Builds and dispatches a KMessage event to all registered listeners.
 *
 * Acquires the notification service lock and, if any listeners exist,
 * constructs an @c IO_SCHEDULER_MONITOR KMessage containing the event code,
 * scheduler pointer, and — when provided — request and operation pointers,
 * then calls NotifyLocked().
 *
 * @param eventCode  One of the IO_SCHEDULER_* event constants (e.g.
 *                   @c IO_SCHEDULER_ADDED, @c IO_SCHEDULER_REMOVED,
 *                   @c IO_SCHEDULER_REQUEST_SCHEDULED, etc.).
 * @param scheduler  The IOScheduler that generated the event; always required.
 * @param request    Optional IORequest associated with the event; may be
 *                   @c NULL.
 * @param operation  Optional IOOperation associated with the event; only
 *                   included when @p request is also non-@c NULL; may be
 *                   @c NULL.
 */
void
IOSchedulerRoster::Notify(uint32 eventCode, const IOScheduler* scheduler,
	IORequest* request, IOOperation* operation)
{
	AutoLocker<DefaultNotificationService> locker(fNotificationService);

	if (!fNotificationService.HasListeners())
		return;

	KMessage event;
	event.SetTo(fEventBuffer, sizeof(fEventBuffer), IO_SCHEDULER_MONITOR);
	event.AddInt32("event", eventCode);
	event.AddPointer("scheduler", scheduler);
	if (request != NULL) {
		event.AddPointer("request", request);
		if (operation != NULL)
			event.AddPointer("operation", operation);
	}

	fNotificationService.NotifyLocked(event, eventCode);
}


/**
 * @brief Returns a roster-unique, monotonically increasing scheduler ID.
 *
 * Acquires fLock, post-increments fNextID, and returns the value. Thread-safe.
 *
 * @return The next available scheduler ID.
 */
int32
IOSchedulerRoster::NextID()
{
	AutoLocker<IOSchedulerRoster> locker(this);
	return fNextID++;
}


//	#pragma mark - debug methods and initialization


/**
 * @brief Prints a human-readable summary of this roster to the kernel
 *        debugger output.
 *
 * Displays the roster's address, mutex pointer, next ID counter, and the
 * addresses of all currently registered schedulers.
 */
void
IOSchedulerRoster::Dump() const
{
	kprintf("IOSchedulerRoster at %p\n", this);
	kprintf("  mutex:   %p\n", &fLock);
	kprintf("  next ID: %" B_PRId32 "\n", fNextID);

	kprintf("  schedulers:");
	for (IOSchedulerList::ConstIterator it
				= fSchedulers.GetIterator();
			IOScheduler* scheduler = it.Next();) {
		kprintf(" %p", scheduler);
	}
	kprintf("\n");
}


/**
 * @brief Kernel debugger command handler — dumps an IOSchedulerRoster.
 *
 * With no argument, dumps the default roster. With one argument, interprets
 * it as a hex address via parse_expression() and dumps that roster.
 *
 * @param argc  Argument count (1 = default roster, 2 = explicit address).
 * @param argv  Argument vector; argv[1] is the optional roster address.
 * @return 0 on success; -1 if the provided address evaluates to @c NULL.
 */
static int
dump_io_scheduler_roster(int argc, char** argv)
{
	IOSchedulerRoster* roster;
	if (argc == 1) {
		roster = IOSchedulerRoster::Default();
	} else if (argc == 2) {
		roster = (IOSchedulerRoster*)parse_expression(argv[1]);
		if (roster == NULL)
			return -1;
	} else {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	roster->Dump();
	return 0;
}


/**
 * @brief Kernel debugger command handler — dumps a single IOScheduler.
 *
 * Expects exactly one argument: the hex address of an IOScheduler. Calls
 * IOScheduler::Dump() on the resolved pointer.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  Argument vector; argv[1] is the scheduler address.
 * @return 0 in all cases (usage is printed if argc != 2).
 */
static int
dump_io_scheduler(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	IOScheduler* scheduler = (IOScheduler*)parse_expression(argv[1]);
	scheduler->Dump();
	return 0;
}


/**
 * @brief Kernel debugger command handler — dumps an IORequestOwner.
 *
 * Expects exactly one argument: the hex address of an IORequestOwner. Calls
 * IORequestOwner::Dump() on the resolved pointer.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  Argument vector; argv[1] is the owner address.
 * @return 0 in all cases (usage is printed if argc != 2).
 */
static int
dump_io_request_owner(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	IORequestOwner* owner = (IORequestOwner*)parse_expression(argv[1]);
	owner->Dump();
	return 0;
}


/**
 * @brief Kernel debugger command handler — dumps an IORequest.
 *
 * Expects exactly one argument: the hex address of an IORequest. Calls
 * IORequest::Dump() on the resolved pointer.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  Argument vector; argv[1] is the request address.
 * @return 0 in all cases (usage or inline help is printed if argc != 2).
 */
static int
dump_io_request(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-io-request>\n", argv[0]);
		return 0;
	}

	IORequest* request = (IORequest*)parse_expression(argv[1]);
	request->Dump();
	return 0;
}


/**
 * @brief Kernel debugger command handler — dumps an IOOperation.
 *
 * Expects exactly one argument: the hex address of an IOOperation. Calls
 * IOOperation::Dump() on the resolved pointer.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  Argument vector; argv[1] is the operation address.
 * @return 0 in all cases (usage or inline help is printed if argc != 2).
 */
static int
dump_io_operation(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-io-operation>\n", argv[0]);
		return 0;
	}

	IOOperation* operation = (IOOperation*)parse_expression(argv[1]);
	operation->Dump();
	return 0;
}


/**
 * @brief Kernel debugger command handler — dumps an IOBuffer.
 *
 * Expects exactly one argument: the hex address of an IOBuffer. Calls
 * IOBuffer::Dump() on the resolved pointer.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  Argument vector; argv[1] is the buffer address.
 * @return 0 in all cases (usage or inline help is printed if argc != 2).
 */
static int
dump_io_buffer(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-io-buffer>\n", argv[0]);
		return 0;
	}

	IOBuffer* buffer = (IOBuffer*)parse_expression(argv[1]);
	buffer->Dump();
	return 0;
}


/**
 * @brief Kernel debugger command handler — dumps a DMABuffer.
 *
 * Expects exactly one argument: the hex address of a DMABuffer. Calls
 * DMABuffer::Dump() on the resolved pointer.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  Argument vector; argv[1] is the DMA buffer address.
 * @return 0 in all cases (usage or inline help is printed if argc != 2).
 */
static int
dump_dma_buffer(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-dma-buffer>\n", argv[0]);
		return 0;
	}

	DMABuffer* buffer = (DMABuffer*)parse_expression(argv[1]);
	buffer->Dump();
	return 0;
}


/**
 * @brief Placement-constructs the default IOSchedulerRoster and registers all
 *        I/O-related kernel debugger commands.
 *
 * Must be called once during kernel initialisation. Registers the following
 * debugger commands:
 * - @c io_scheduler_roster — dump an IOSchedulerRoster
 * - @c io_scheduler        — dump an IOScheduler
 * - @c io_request_owner    — dump an IORequestOwner
 * - @c io_request          — dump an IORequest
 * - @c io_operation        — dump an IOOperation
 * - @c io_buffer           — dump an IOBuffer
 * - @c dma_buffer          — dump a DMABuffer
 */
/*static*/ void
IOSchedulerRoster::Init()
{
	new(&sDefaultInstance) IOSchedulerRoster;

	add_debugger_command_etc("io_scheduler_roster", &dump_io_scheduler_roster,
		"Dump an I/O scheduler roster",
		"<scheduler-roster>\n"
		"Dumps I/O scheduler roster at address <scheduler-roster>.\n"
		"If unspecified, dump the default roster.\n", 0);
	add_debugger_command_etc("io_scheduler", &dump_io_scheduler,
		"Dump an I/O scheduler",
		"<scheduler>\n"
		"Dumps I/O scheduler at address <scheduler>.\n", 0);
	add_debugger_command_etc("io_request_owner", &dump_io_request_owner,
		"Dump an I/O request owner",
		"<owner>\n"
		"Dumps I/O request owner at address <owner>.\n", 0);
	add_debugger_command("io_request", &dump_io_request, "dump an I/O request");
	add_debugger_command("io_operation", &dump_io_operation,
		"dump an I/O operation");
	add_debugger_command("io_buffer", &dump_io_buffer, "dump an I/O buffer");
	add_debugger_command("dma_buffer", &dump_dma_buffer, "dump a DMA buffer");
}
