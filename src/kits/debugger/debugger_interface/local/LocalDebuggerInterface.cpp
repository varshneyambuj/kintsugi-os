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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2010-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file LocalDebuggerInterface.cpp
 * @brief DebuggerInterface implementation talking to the local kernel debugger nub.
 *
 * Owns one debugger port for events plus a pool of short-lived debug contexts
 * that get borrowed for synchronous control calls (read/write memory,
 * install breakpoint, get CPU state, ...). The pool is needed because the
 * kernel-side debug syscalls hold per-context reply ports for the duration of
 * a request and serializing them all through one context would block the
 * event stream.
 */


#include "LocalDebuggerInterface.h"

#include <new>

#include <stdio.h>

#include <Locker.h>

#include <AutoLocker.h>
#include <memory_private.h>
#include <OS.h>
#include <system_info.h>
#include <util/DoublyLinkedList.h>
#include <kernel/util/KMessage.h>

#include "debug_utils.h"

#include "ArchitectureX86.h"
#include "ArchitectureX8664.h"
#include "AreaInfo.h"
#include "AutoDeleter.h"
#include "CpuState.h"
#include "DebugEvent.h"
#include "ImageInfo.h"
#include "SemaphoreInfo.h"
#include "SymbolInfo.h"
#include "SystemInfo.h"
#include "TeamInfo.h"
#include "ThreadInfo.h"


/** @brief Initial size of the debug-context pool created at Init() time. */
static const int kInitialDebugContextCount = 3;

/** @brief Hard cap on debug-context pool size; threads block once it is reached. */
static const int kMaxDebugContextCount = 10;


// #pragma mark - LocalDebuggerInterface::DebugContext

/**
 * @brief Wrapper around the kernel debug_context structure that keeps it
 *        teardown-safe and provides an Init/Close pair tied to the parent's
 *        port lifetime.
 */
struct LocalDebuggerInterface::DebugContext : debug_context,
		DoublyLinkedListLinkImpl<DebugContext> {
	/** @brief Default-constructs a context with all kernel ids cleared. */
	DebugContext()
	{
		team = -1;
		nub_port = -1;
		reply_port = -1;
	}

	/** @brief Destroys the kernel debug context if still live. */
	~DebugContext()
	{
		if (reply_port >= 0)
			destroy_debug_context(this);
	}

	/**
	 * @brief Initializes the underlying kernel debug context.
	 *
	 * @param team     Team this context targets.
	 * @param nubPort  Port of the team's debugger nub.
	 * @return Status from init_debug_context().
	 */
	status_t Init(team_id team, port_id nubPort)
	{
		return init_debug_context(this, team, nubPort);
	}

	/** @brief Tears down the kernel debug context and resets the ids. */
	void Close()
	{
		if (reply_port >= 0) {
			destroy_debug_context(this);
			team = -1;
			nub_port = -1;
			reply_port = -1;
		}
	}
};

// #pragma mark - LocalDebuggerInterface::DebugContextPool

/**
 * @brief Bounded pool of DebugContext objects that callers borrow for
 *        synchronous nub requests and return when finished.
 *
 * Grows on demand up to kMaxDebugContextCount; threads requesting a context
 * once the cap is reached block on a semaphore until one is returned.
 */
struct LocalDebuggerInterface::DebugContextPool {
	/**
	 * @brief Constructs an empty pool bound to a specific team and nub port.
	 */
	DebugContextPool(team_id team, port_id nubPort)
		:
		fLock("debug context pool"),
		fTeam(team),
		fNubPort(nubPort),
		fBlockSem(-1),
		fContextCount(0),
		fWaiterCount(0),
		fClosed(false)
	{
	}

	/** @brief Drains the free list and deletes the blocking semaphore. */
	~DebugContextPool()
	{
		AutoLocker<BLocker> locker(fLock);

		while (DebugContext* context = fFreeContexts.RemoveHead())
			delete context;

		if (fBlockSem >= 0)
			delete_sem(fBlockSem);
	}

	/**
	 * @brief Performs late initialization, pre-creating kInitialDebugContextCount contexts.
	 *
	 * @return B_OK on success, or any error from the locker, semaphore, or
	 *         per-context init.
	 */
	status_t Init()
	{
		status_t error = fLock.InitCheck();
		if (error != B_OK)
			return error;

		fBlockSem = create_sem(0, "debug context pool block");
		if (fBlockSem < 0)
			return fBlockSem;

		for (int i = 0; i < kInitialDebugContextCount; i++) {
			DebugContext* context;
			error = _CreateDebugContext(context);
			if (error != B_OK)
				return error;

			fFreeContexts.Add(context);
		}

		return B_OK;
	}

	/**
	 * @brief Marks the pool closed and tears down every contained kernel context.
	 *
	 * Used during shutdown so that any thread still holding a borrowed context
	 * can release it without re-acquiring kernel resources.
	 */
	void Close()
	{
		AutoLocker<BLocker> locker(fLock);
		fClosed = true;

		for (DebugContextList::Iterator it = fFreeContexts.GetIterator();
				DebugContext* context = it.Next();) {
			context->Close();
		}

		for (DebugContextList::Iterator it = fUsedContexts.GetIterator();
				DebugContext* context = it.Next();) {
			context->Close();
		}
	}

	/**
	 * @brief Borrows a free context, creating a new one or blocking as required.
	 *
	 * @return Pointer to a borrowed DebugContext; never NULL because the call
	 *         blocks on @c fBlockSem until one becomes available.
	 */
	DebugContext* GetContext()
	{
		AutoLocker<BLocker> locker(fLock);
		DebugContext* context = fFreeContexts.RemoveHead();

		if (context == NULL) {
			if (fContextCount >= kMaxDebugContextCount
				|| _CreateDebugContext(context) != B_OK) {
				// wait for a free context
				while (context == NULL) {
					fWaiterCount++;
					locker.Unlock();
					while (acquire_sem(fBlockSem) != B_OK);
					locker.Lock();
					context = fFreeContexts.RemoveHead();
				}
			}
		}

		fUsedContexts.Add(context);

		return context;
	}

	/**
	 * @brief Returns @a context to the free list and wakes one waiter if present.
	 *
	 * @param context  Context previously borrowed via GetContext().
	 */
	void PutContext(DebugContext* context)
	{
		AutoLocker<BLocker> locker(fLock);
		fUsedContexts.Remove(context);
		fFreeContexts.Add(context);

		if (fWaiterCount > 0)
			release_sem(fBlockSem);
	}

private:
	typedef DoublyLinkedList<DebugContext> DebugContextList;

private:
	/**
	 * @brief Allocates a fresh debug context, optionally initializing it
	 *        against the kernel nub.
	 *
	 * @param _context  Output pointer to the new context.
	 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
	 *         from DebugContext::Init().
	 */
	status_t _CreateDebugContext(DebugContext*& _context)
	{
		DebugContext* context = new(std::nothrow) DebugContext;
		if (context == NULL)
			return B_NO_MEMORY;

		if (!fClosed) {
			status_t error = context->Init(fTeam, fNubPort);
			if (error != B_OK) {
				delete context;
				return error;
			}
		}

		fContextCount++;

		_context = context;
		return B_OK;
	}

private:
	BLocker				fLock;
	team_id				fTeam;
	port_id				fNubPort;
	sem_id				fBlockSem;
	int32				fContextCount;
	int32				fWaiterCount;
	DebugContextList	fFreeContexts;
	DebugContextList	fUsedContexts;
	bool				fClosed;
};


/**
 * @brief RAII helper that borrows a DebugContext on construction and returns
 *        it on destruction, simplifying error paths in synchronous calls.
 */
struct LocalDebuggerInterface::DebugContextGetter {
	/** @brief Borrows a context from @a pool. */
	DebugContextGetter(DebugContextPool* pool)
		:
		fPool(pool),
		fContext(pool->GetContext())
	{
	}

	/** @brief Returns the borrowed context to the pool. */
	~DebugContextGetter()
	{
		fPool->PutContext(fContext);
	}

	/** @brief Returns the borrowed context. */
	DebugContext* Context() const
	{
		return fContext;
	}

private:
	DebugContextPool*	fPool;
	DebugContext*		fContext;
};

// #pragma mark - LocalDebuggerInterface

/**
 * @brief Constructs the interface bound to a specific team; Init() must follow.
 *
 * @param team  Team id this interface will debug.
 */
LocalDebuggerInterface::LocalDebuggerInterface(team_id team)
	:
	DebuggerInterface(),
	fTeamID(team),
	fDebuggerPort(-1),
	fNubPort(-1),
	fDebugContextPool(NULL),
	fArchitecture(NULL)
{
}


/**
 * @brief Releases the architecture, removes the team debugger, and tears
 *        down the debug-context pool.
 */
LocalDebuggerInterface::~LocalDebuggerInterface()
{
	if (fArchitecture != NULL)
		fArchitecture->ReleaseReference();

	Close(false);

	delete fDebugContextPool;
}


/**
 * @brief Initializes architecture, debugger port, team-debugger registration,
 *        thread-property watching, and the debug-context pool.
 *
 * @return B_OK on success, B_UNSUPPORTED on unrecognized architectures,
 *         B_NO_MEMORY for allocation failures, or any underlying kernel error.
 */
status_t
LocalDebuggerInterface::Init()
{
	// create the architecture
#if defined(ARCH_x86)
	fArchitecture = new(std::nothrow) ArchitectureX86(this);
#elif defined(ARCH_x86_64)
	fArchitecture = new(std::nothrow) ArchitectureX8664(this);
#else
	return B_UNSUPPORTED;
#endif

	if (fArchitecture == NULL)
		return B_NO_MEMORY;

	status_t error = fArchitecture->Init();
	if (error != B_OK)
		return error;

	// create debugger port
	char buffer[128];
	snprintf(buffer, sizeof(buffer), "team %" B_PRId32 " debugger", fTeamID);
	fDebuggerPort = create_port(100, buffer);
	if (fDebuggerPort < 0)
		return fDebuggerPort;

	// install as team debugger
	fNubPort = install_team_debugger(fTeamID, fDebuggerPort);
	if (fNubPort < 0)
		return fNubPort;

	error = __start_watching_system(fTeamID, B_WATCH_SYSTEM_THREAD_PROPERTIES,
		fDebuggerPort, 0);
	if (error != B_OK)
		return error;

	// create debug context pool
	fDebugContextPool = new(std::nothrow) DebugContextPool(fTeamID, fNubPort);
	if (fDebugContextPool == NULL)
		return B_NO_MEMORY;

	error = fDebugContextPool->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Detaches the debugger from the team and releases ports.
 *
 * @param killTeam  If true the team is killed; otherwise debugging is simply
 *                  removed and the team continues to run.
 */
void
LocalDebuggerInterface::Close(bool killTeam)
{
	if (killTeam)
		kill_team(fTeamID);
	else if (fNubPort >= 0)
		remove_team_debugger(fTeamID);

	if (fDebuggerPort >= 0) {
		__stop_watching_system(fTeamID, B_WATCH_SYSTEM_THREAD_PROPERTIES,
			fDebuggerPort, 0);
		delete_port(fDebuggerPort);
	}

	fNubPort = -1;
	fDebuggerPort = -1;
}


/**
 * @brief Reports whether the team is currently being debugged via this interface.
 *
 * @return true if the kernel nub port is live, false otherwise.
 */
bool
LocalDebuggerInterface::Connected() const
{
	return fNubPort >= 0;
}


/**
 * @brief Returns the team id this interface is bound to.
 */
team_id
LocalDebuggerInterface::TeamID() const
{
	return fTeamID;
}


/**
 * @brief Returns the Architecture object associated with the team.
 */
Architecture*
LocalDebuggerInterface::GetArchitecture() const
{
	return fArchitecture;
}


/**
 * @brief Reads the next event from the debugger port and converts it to a
 *        DebugEvent the rest of the debugger understands.
 *
 * Loops past events the higher layers should ignore (for example transient
 * thread state changes) and past synthetic system-watch messages, which are
 * unpacked through _GetNextSystemWatchEvent().
 *
 * @param _event  On success, set to a freshly-allocated DebugEvent owned by
 *                the caller.
 * @return B_OK on success or any port/event-construction error.
 */
status_t
LocalDebuggerInterface::GetNextDebugEvent(DebugEvent*& _event)
{
	while (true) {
		char buffer[2048];
		int32 messageCode;
		ssize_t size = read_port(fDebuggerPort, &messageCode, buffer,
			sizeof(buffer));
		if (size < 0) {
			if (size == B_INTERRUPTED)
				continue;

			return size;
		}

		if (messageCode <= B_DEBUGGER_MESSAGE_HANDED_OVER) {
 			debug_debugger_message_data message;
			memcpy(&message, buffer, size);
			if (message.origin.team != fTeamID)
				continue;

			bool ignore = false;
			status_t error = _CreateDebugEvent(messageCode, message, ignore,
				_event);
			if (error != B_OK)
				return error;

			if (ignore) {
				if (message.origin.thread >= 0 && message.origin.nub_port >= 0)
					error = continue_thread(message.origin.nub_port,
						message.origin.thread);
				if (error != B_OK)
					return error;
				continue;
			}

			return B_OK;
		}

		KMessage message;
		size = message.SetTo(buffer);
		if (size != B_OK)
			return size;
		return _GetNextSystemWatchEvent(_event, message);
	}

	return B_OK;
}


/**
 * @brief Updates the team's kernel-side debug flags.
 *
 * @param flags  Bitmask of B_TEAM_DEBUG_* flags.
 * @return Result from set_team_debugging_flags().
 */
status_t
LocalDebuggerInterface::SetTeamDebuggingFlags(uint32 flags)
{
	return set_team_debugging_flags(fNubPort, flags);
}


/**
 * @brief Resumes a stopped thread.
 *
 * @param thread  Thread id to resume.
 * @return Result from continue_thread().
 */
status_t
LocalDebuggerInterface::ContinueThread(thread_id thread)
{
	return continue_thread(fNubPort, thread);
}


/**
 * @brief Stops a running thread by asking the kernel to debug it.
 *
 * @param thread  Thread id to stop.
 * @return Result from debug_thread().
 */
status_t
LocalDebuggerInterface::StopThread(thread_id thread)
{
	return debug_thread(thread);
}


/**
 * @brief Single-steps a thread by issuing a continue with the single-step flag.
 *
 * @param thread  Thread id to step.
 * @return Result from write_port() to the nub.
 */
status_t
LocalDebuggerInterface::SingleStepThread(thread_id thread)
{
	debug_nub_continue_thread continueMessage;
	continueMessage.thread = thread;
	continueMessage.handle_event = B_THREAD_DEBUG_HANDLE_EVENT;
	continueMessage.single_step = true;

	return write_port(fNubPort, B_DEBUG_MESSAGE_CONTINUE_THREAD,
		&continueMessage, sizeof(continueMessage));
}


/**
 * @brief Installs a breakpoint at @a address via the kernel nub.
 *
 * Borrows a debug context from the pool to receive the synchronous reply.
 *
 * @param address  Target address to break on.
 * @return B_OK on success, the nub's error if the install failed, or any
 *         transport error from send_debug_message().
 */
status_t
LocalDebuggerInterface::InstallBreakpoint(target_addr_t address)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_set_breakpoint message;
	message.reply_port = contextGetter.Context()->reply_port;
	message.address = (void*)(addr_t)address;

	debug_nub_set_breakpoint_reply reply;

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_SET_BREAKPOINT, &message, sizeof(message), &reply,
		sizeof(reply));
	return error == B_OK ? reply.error : error;
}


/**
 * @brief Removes a breakpoint at @a address via the kernel nub.
 *
 * Fire-and-forget: the kernel's clear-breakpoint path needs no reply.
 *
 * @param address  Target address whose breakpoint should be removed.
 * @return Result from write_port().
 */
status_t
LocalDebuggerInterface::UninstallBreakpoint(target_addr_t address)
{
	debug_nub_clear_breakpoint message;
	message.address = (void*)(addr_t)address;

	return write_port(fNubPort, B_DEBUG_MESSAGE_CLEAR_BREAKPOINT,
		&message, sizeof(message));
}


/**
 * @brief Installs a hardware watchpoint via the kernel nub.
 *
 * @param address  Target address to watch.
 * @param type     B_DEBUG_WATCHPOINT_* trigger type (read, write, access).
 * @param length   Watch length in bytes.
 * @return B_OK on success, the nub's error if the install failed, or any
 *         transport error from send_debug_message().
 */
status_t
LocalDebuggerInterface::InstallWatchpoint(target_addr_t address, uint32 type,
	int32 length)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_set_watchpoint message;
	message.reply_port = contextGetter.Context()->reply_port;
	message.address = (void*)(addr_t)address;
	message.type = type;
	message.length = length;

	debug_nub_set_watchpoint_reply reply;

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_SET_WATCHPOINT, &message, sizeof(message), &reply,
		sizeof(reply));
	return error == B_OK ? reply.error : error;
}


/**
 * @brief Removes a hardware watchpoint via the kernel nub.
 *
 * @param address  Target address whose watchpoint should be removed.
 * @return Result from write_port().
 */
status_t
LocalDebuggerInterface::UninstallWatchpoint(target_addr_t address)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_clear_watchpoint message;
	message.address = (void*)(addr_t)address;

	return write_port(fNubPort, B_DEBUG_MESSAGE_CLEAR_WATCHPOINT,
		&message, sizeof(message));
}


/**
 * @brief Populates @a info with the host's system information and uname data.
 *
 * @param info  Output parameter populated on success.
 * @return B_OK on success, otherwise the first failing status from
 *         get_system_info() or uname().
 */
status_t
LocalDebuggerInterface::GetSystemInfo(SystemInfo& info)
{
	system_info sysInfo;
	status_t result = get_system_info(&sysInfo);
	if (result != B_OK)
		return result;

	utsname name;
	result = uname(&name);
	if (result != B_OK)
		return result;

	info.SetTo(fTeamID, sysInfo, name);
	return B_OK;
}


/**
 * @brief Populates @a info from the team's kernel-side team_info.
 *
 * @param info  Output parameter populated on success.
 * @return B_OK on success or any error from get_team_info().
 */
status_t
LocalDebuggerInterface::GetTeamInfo(TeamInfo& info)
{
	team_info teamInfo;
	status_t result = get_team_info(fTeamID, &teamInfo);
	if (result != B_OK)
		return result;

	info.SetTo(fTeamID, teamInfo);
	return B_OK;
}


/**
 * @brief Builds a list of ThreadInfo objects for every thread in the team.
 *
 * @param infos  Output list; ownership of the appended ThreadInfo objects transfers.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
LocalDebuggerInterface::GetThreadInfos(BObjectList<ThreadInfo, true>& infos)
{
	thread_info threadInfo;
	int32 cookie = 0;
	while (get_next_thread_info(fTeamID, &cookie, &threadInfo) == B_OK) {
		ThreadInfo* info = new(std::nothrow) ThreadInfo(threadInfo.team,
			threadInfo.thread, threadInfo.name);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Builds a list of ImageInfo objects for every image loaded in the team.
 *
 * @param infos  Output list; ownership of the appended ImageInfo objects transfers.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
LocalDebuggerInterface::GetImageInfos(BObjectList<ImageInfo, true>& infos)
{
	// get the team's images
	image_info imageInfo;
	int32 cookie = 0;
	while (get_next_image_info(fTeamID, &cookie, &imageInfo) == B_OK) {
		ImageInfo* info = new(std::nothrow) ImageInfo(fTeamID, imageInfo.id,
			imageInfo.name, imageInfo.type, (addr_t)imageInfo.text,
			imageInfo.text_size, (addr_t)imageInfo.data, imageInfo.data_size);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Builds a list of AreaInfo objects for every area mapped by the team.
 *
 * @param infos  Output list; ownership of the appended AreaInfo objects transfers.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
LocalDebuggerInterface::GetAreaInfos(BObjectList<AreaInfo, true>& infos)
{
	// get the team's areas
	area_info areaInfo;
	ssize_t cookie = 0;
	while (get_next_area_info(fTeamID, &cookie, &areaInfo) == B_OK) {
		AreaInfo* info = new(std::nothrow) AreaInfo(fTeamID, areaInfo.area,
			areaInfo.name, (addr_t)areaInfo.address, areaInfo.size,
			areaInfo.ram_size, areaInfo.lock, areaInfo.protection);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Builds a list of SemaphoreInfo objects for every semaphore owned by the team.
 *
 * @param infos  Output list; ownership of the appended SemaphoreInfo objects transfers.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
LocalDebuggerInterface::GetSemaphoreInfos(BObjectList<SemaphoreInfo, true>& infos)
{
	// get the team's semaphores
	sem_info semInfo;
	int32 cookie = 0;
	while (get_next_sem_info(fTeamID, &cookie, &semInfo) == B_OK) {
		SemaphoreInfo* info = new(std::nothrow) SemaphoreInfo(fTeamID,
			semInfo.sem, semInfo.name, semInfo.count, semInfo.latest_holder);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Enumerates every symbol in @a image into a list via the kernel's
 *        debug-symbol APIs.
 *
 * @param team   Team id (passed for API symmetry; the kernel uses the
 *               interface's bound team).
 * @param image  Image identifier to iterate over.
 * @param infos  Output list; ownership of the appended SymbolInfo objects transfers.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails, or any error
 *         from the debug_*_symbol_lookup APIs.
 */
status_t
LocalDebuggerInterface::GetSymbolInfos(team_id team, image_id image,
	BObjectList<SymbolInfo, true>& infos)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	// create a lookup context
	debug_symbol_lookup_context* lookupContext;
	status_t error = debug_create_symbol_lookup_context(contextGetter.Context(),
		image, &lookupContext);
	if (error != B_OK)
		return error;

	// create a symbol iterator
	debug_symbol_iterator* iterator;
	error = debug_create_image_symbol_iterator(
		lookupContext, image, &iterator);
	if (error != B_OK) {
		debug_delete_symbol_lookup_context(lookupContext);
		return error;
	}

	// get the symbols
	char name[1024];
	int32 type;
	void* address;
	size_t size;
	while (debug_next_image_symbol(iterator, name, sizeof(name), &type,
			&address, &size) == B_OK) {
		SymbolInfo* info = new(std::nothrow) SymbolInfo(
			(target_addr_t)(addr_t)address, size, type, name);
		if (info == NULL)
			break;
		if (!infos.AddItem(info)) {
			delete info;
			break;
		}
	}

	// delete the symbol iterator and lookup context
	debug_delete_symbol_iterator(iterator);
	debug_delete_symbol_lookup_context(lookupContext);

	return B_OK;
}


/**
 * @brief Looks up a single symbol in @a image by name via the kernel's
 *        debug-symbol APIs.
 *
 * @param team        Team id (passed for API symmetry).
 * @param image       Image identifier to search.
 * @param name        Symbol name to look up.
 * @param symbolType  Filter for the symbol type (B_SYMBOL_TYPE_*).
 * @param info        Output parameter populated on success.
 * @return B_OK on success or any error from debug_get_symbol().
 */
status_t
LocalDebuggerInterface::GetSymbolInfo(team_id team, image_id image, const char* name,
	int32 symbolType, SymbolInfo& info)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	// create a lookup context
	debug_symbol_lookup_context* lookupContext;
	status_t error = debug_create_symbol_lookup_context(contextGetter.Context(),
		image, &lookupContext);
	if (error != B_OK)
		return error;

	// try to get the symbol
	void* foundAddress;
	size_t foundSize;
	int32 foundType;
	error = debug_get_symbol(lookupContext, image, name, symbolType,
		&foundAddress, &foundSize, &foundType);
	if (error == B_OK) {
		info.SetTo((target_addr_t)(addr_t)foundAddress, foundSize, foundType,
			name);
	}

	// delete the lookup context
	debug_delete_symbol_lookup_context(lookupContext);

	return error;
}


/**
 * @brief Returns the per-thread info (team id, thread id, name) for @a thread.
 *
 * @param thread  Thread id to query.
 * @param info    Output parameter populated on success.
 * @return B_OK on success or any error from get_thread_info().
 */
status_t
LocalDebuggerInterface::GetThreadInfo(thread_id thread, ThreadInfo& info)
{
	thread_info threadInfo;
	status_t error = get_thread_info(thread, &threadInfo);
	if (error != B_OK)
		return error;

	info.SetTo(threadInfo.team, threadInfo.thread, threadInfo.name);
	return B_OK;
}


/**
 * @brief Reads the kernel's CPU state for a thread and wraps it as a CpuState.
 *
 * @param thread  Thread id to query.
 * @param _state  On success, set to a freshly-allocated CpuState owned by the caller.
 * @return B_OK on success, or any error from the kernel/getter or
 *         Architecture::CreateCpuState().
 */
status_t
LocalDebuggerInterface::GetCpuState(thread_id thread, CpuState*& _state)
{
	debug_cpu_state debugState;
	status_t error = _GetDebugCpuState(thread, debugState);
	if (error != B_OK)
		return error;
	return fArchitecture->CreateCpuState(&debugState, sizeof(debug_cpu_state),
		_state);
}


/**
 * @brief Writes a CpuState back to the kernel for a thread.
 *
 * Reads the current debug_cpu_state, asks @a state to merge its values into
 * it, then sends it to the nub.
 *
 * @param thread  Thread id to update.
 * @param state   New CPU state.
 * @return B_OK on success, or any error from the kernel APIs or the merge.
 */
status_t
LocalDebuggerInterface::SetCpuState(thread_id thread, const CpuState* state)
{
	debug_cpu_state debugState;
	status_t error = _GetDebugCpuState(thread, debugState);
	if (error != B_OK)
		return error;

	DebugContextGetter contextGetter(fDebugContextPool);

	error = state->UpdateDebugState(&debugState, sizeof(debugState));
	if (error != B_OK)
		return error;

	debug_nub_set_cpu_state message;
	message.thread = thread;

	memcpy(&message.cpu_state, &debugState, sizeof(debugState));

	return send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_SET_CPU_STATE, &message, sizeof(message), NULL,
		0);
}


/**
 * @brief Reports CPU feature flags as discovered by the Architecture object.
 *
 * @param flags  Output parameter populated by the Architecture.
 * @return Whatever Architecture::GetCpuFeatures() returns.
 */
status_t
LocalDebuggerInterface::GetCpuFeatures(uint32& flags)
{
	return fArchitecture->GetCpuFeatures(flags);
}


/**
 * @brief Asks the kernel nub to write the team's state to a core file at @a path.
 *
 * @param path  Filesystem path the kernel will write to.
 * @return B_OK on success, the nub's error if the write failed, or any
 *         transport error from send_debug_message().
 */
status_t
LocalDebuggerInterface::WriteCoreFile(const char* path)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_write_core_file_reply reply;

	debug_nub_write_core_file message;
	message.reply_port = contextGetter.Context()->reply_port;
	strlcpy(message.path, path, sizeof(message.path));

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_WRITE_CORE_FILE, &message, sizeof(message), &reply,
		sizeof(reply));
	if (error == B_OK)
		error = reply.error;

	return error;
}


/**
 * @brief Returns the protection and locking flags for the area covering @a address.
 *
 * @param address     Target-side address to look up.
 * @param protection  Output parameter; receives the area's protection flags.
 * @param locking     Output parameter; receives the area's locking flags.
 * @return Result from get_memory_properties().
 */
status_t
LocalDebuggerInterface::GetMemoryProperties(target_addr_t address,
	uint32& protection, uint32& locking)
{
	return get_memory_properties(fTeamID, (const void *)address,
		&protection, &locking);
}


/**
 * @brief Reads up to @a size bytes from the team's address space at @a address.
 *
 * @param address  Target-side starting address.
 * @param buffer   Destination buffer; must hold @a size bytes.
 * @param size     Number of bytes to read.
 * @return Number of bytes read, or a negative error code on failure.
 */
ssize_t
LocalDebuggerInterface::ReadMemory(target_addr_t address, void* buffer, size_t size)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	return debug_read_memory(contextGetter.Context(),
		(const void*)(addr_t)address, buffer, size);
}


/**
 * @brief Writes up to @a size bytes into the team's address space at @a address.
 *
 * @param address  Target-side starting address.
 * @param buffer   Source buffer.
 * @param size     Number of bytes to write.
 * @return Number of bytes written, or a negative error code on failure.
 */
ssize_t
LocalDebuggerInterface::WriteMemory(target_addr_t address, void* buffer,
	size_t size)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	return debug_write_memory(contextGetter.Context(),
		(const void*)address, buffer, size);
}


/**
 * @brief Converts a raw kernel debugger message into the appropriate
 *        DebugEvent subclass.
 *
 * Handles every B_DEBUGGER_MESSAGE_* code by allocating the right event type
 * and copying out the relevant payload (CPU state, signal info, image info,
 * syscall info, ...). Sets @a _ignore for events the higher layers should
 * filter out (for example pre-syscall stops with no payload of interest).
 *
 * @param messageCode  Original debugger-message code.
 * @param message      Raw message payload from the debugger port.
 * @param _ignore      Output flag; true if the caller should drop the event
 *                     and read another.
 * @param _event       On success and when not ignoring, set to a freshly-
 *                     allocated DebugEvent owned by the caller.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
 *         from CpuState construction.
 */
status_t
LocalDebuggerInterface::_CreateDebugEvent(int32 messageCode,
	const debug_debugger_message_data& message, bool& _ignore,
	DebugEvent*& _event)
{
	DebugEvent* event = NULL;

	switch (messageCode) {
		case B_DEBUGGER_MESSAGE_THREAD_DEBUGGED:
			event = new(std::nothrow) ThreadDebuggedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_DEBUGGER_CALL:
			event = new(std::nothrow) DebuggerCallEvent(message.origin.team,
				message.origin.thread,
				(target_addr_t)message.debugger_call.message);
			break;
		case B_DEBUGGER_MESSAGE_BREAKPOINT_HIT:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.breakpoint_hit.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) BreakpointHitEvent(message.origin.team,
				message.origin.thread, state);
			state->ReleaseReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_WATCHPOINT_HIT:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.watchpoint_hit.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) WatchpointHitEvent(message.origin.team,
				message.origin.thread, state);
			state->ReleaseReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_SINGLE_STEP:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.single_step.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) SingleStepEvent(message.origin.team,
				message.origin.thread, state);
			state->ReleaseReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED:
			event = new(std::nothrow) ExceptionOccurredEvent(
				message.origin.team, message.origin.thread,
				message.exception_occurred.exception);
			break;
		case B_DEBUGGER_MESSAGE_TEAM_DELETED:
			if (message.origin.team != fTeamID) {
				_ignore = true;
				return B_OK;
			}
			event = new(std::nothrow) TeamDeletedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_TEAM_EXEC:
			if (message.origin.team != fTeamID) {
				_ignore = true;
				return B_OK;
			}
			event = new(std::nothrow) TeamExecEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_THREAD_CREATED:
			event = new(std::nothrow) ThreadCreatedEvent(message.origin.team,
				message.origin.thread, message.thread_created.new_thread);
			break;
		case B_DEBUGGER_MESSAGE_THREAD_DELETED:
			event = new(std::nothrow) ThreadDeletedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_IMAGE_CREATED:
		{
			const image_info& info = message.image_created.info;
			event = new(std::nothrow) ImageCreatedEvent(message.origin.team,
				message.origin.thread,
				ImageInfo(fTeamID, info.id, info.name, info.type,
					(addr_t)info.text, info.text_size, (addr_t)info.data,
					info.data_size));
			break;
		}
		case B_DEBUGGER_MESSAGE_IMAGE_DELETED:
		{
			const image_info& info = message.image_deleted.info;
			event = new(std::nothrow) ImageDeletedEvent(message.origin.team,
				message.origin.thread,
				ImageInfo(fTeamID, info.id, info.name, info.type,
					(addr_t)info.text, info.text_size, (addr_t)info.data,
					info.data_size));
			break;
		}
		case B_DEBUGGER_MESSAGE_POST_SYSCALL:
		{
			event = new(std::nothrow) PostSyscallEvent(message.origin.team,
				message.origin.thread,
				SyscallInfo(message.post_syscall.start_time,
					message.post_syscall.end_time,
					message.post_syscall.return_value,
					message.post_syscall.syscall, message.post_syscall.args));
			break;
		}
		case B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED:
		{
			event = new(std::nothrow) SignalReceivedEvent(message.origin.team,
				message.origin.thread,
				SignalInfo(message.signal_received.signal,
					message.signal_received.handler,
					message.signal_received.deadly));
			break;
		}
		default:
			printf("DebuggerInterface for team %" B_PRId32 ": unknown message "
				"from kernel: %" B_PRId32 "\n", fTeamID, messageCode);
			// fall through...
		case B_DEBUGGER_MESSAGE_TEAM_CREATED:
		case B_DEBUGGER_MESSAGE_PRE_SYSCALL:
		case B_DEBUGGER_MESSAGE_PROFILER_UPDATE:
		case B_DEBUGGER_MESSAGE_HANDED_OVER:
			_ignore = true;
			return B_OK;
	}

	if (event == NULL)
		return B_NO_MEMORY;

	if (message.origin.thread >= 0 && message.origin.nub_port >= 0)
		event->SetThreadStopped(true);

	_ignore = false;
	_event = event;

	return B_OK;
}


/**
 * @brief Decodes a B_SYSTEM_OBJECT_UPDATE message into a synthetic DebugEvent.
 *
 * Currently only B_THREAD_NAME_CHANGED is mapped (to a ThreadRenamedEvent);
 * other opcodes return B_BAD_DATA so the caller can keep reading events.
 *
 * @param _event   On success, set to a freshly-allocated DebugEvent owned by
 *                 the caller; left untouched on error.
 * @param message  Parsed system-watch KMessage.
 * @return B_OK on a successful conversion, B_BAD_DATA for unsupported codes
 *         or malformed messages, or any error from get_thread_info().
 */
status_t
LocalDebuggerInterface::_GetNextSystemWatchEvent(DebugEvent*& _event,
	KMessage& message)
{
	status_t error = B_OK;
	if (message.What() != B_SYSTEM_OBJECT_UPDATE)
		return B_BAD_DATA;

	int32 opcode = 0;
	if (message.FindInt32("opcode", &opcode) != B_OK)
		return B_BAD_DATA;

	DebugEvent* event = NULL;
	switch (opcode)
	{
		case B_THREAD_NAME_CHANGED:
		{
			int32 threadID = -1;
			if (message.FindInt32("thread", &threadID) != B_OK)
				break;

			thread_info info;
			error = get_thread_info(threadID, &info);
			if (error != B_OK)
				break;

			event = new(std::nothrow) ThreadRenamedEvent(fTeamID,
				threadID, threadID, info.name);
			break;
		}

		default:
		{
			error = B_BAD_DATA;
			break;
		}
	}

	if (event != NULL)
		_event = event;

	return error;
}


/**
 * @brief Synchronously fetches the kernel debug_cpu_state for a thread.
 *
 * @param thread  Thread id to query.
 * @param _state  Output parameter filled with the kernel's CPU state on success.
 * @return B_OK on success, the nub's error if the request failed, or any
 *         transport error from send_debug_message().
 */
status_t
LocalDebuggerInterface::_GetDebugCpuState(thread_id thread, debug_cpu_state& _state)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_get_cpu_state message;
	message.reply_port = contextGetter.Context()->reply_port;
	message.thread = thread;

	debug_nub_get_cpu_state_reply reply;

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_GET_CPU_STATE, &message, sizeof(message), &reply,
		sizeof(reply));
	if (error != B_OK)
		return error;
	if (reply.error != B_OK)
		return reply.error;

	memcpy(&_state, &reply.cpu_state, sizeof(debug_cpu_state));

	return B_OK;
}
