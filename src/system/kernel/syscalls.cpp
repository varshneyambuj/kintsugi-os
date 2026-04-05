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
 *   Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2004-2010, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file syscalls.cpp
 * @brief Syscall dispatch table and generic syscall interface.
 *
 * Contains the big switch statement that maps syscall numbers to their
 * kernel handler functions, and implements the generic_syscall() mechanism
 * that allows add-ons to register additional syscall handlers at runtime.
 *
 * @see thread.cpp, vfs.cpp
 */


#include <syscalls.h>

#include <stdlib.h>
#include <string.h>

#include <TypeConstants.h>

#include <arch_config.h>
#include <arch/system_info.h>
#include <cpu.h>
#include <debug.h>
#include <disk_device_manager/ddm_userland_interface.h>
#include <elf.h>
#include <event_queue.h>
#include <frame_buffer_console.h>
#include <fs/fd.h>
#include <fs/node_monitor.h>
#include <generic_syscall.h>
#include <interrupts.h>
#include <kernel.h>
#include <kimage.h>
#include <ksignal.h>
#include <ksyscalls.h>
#include <ksystem_info.h>
#include <messaging.h>
#include <port.h>
#include <posix/realtime_sem.h>
#include <posix/xsi_message_queue.h>
#include <posix/xsi_semaphore.h>
#include <real_time_clock.h>
#include <safemode.h>
#include <sem.h>
#include <sys/resource.h>
#include <system_profiler.h>
#include <thread.h>
#include <tracing.h>
#include <user_atomic.h>
#include <user_mutex.h>
#include <usergroup.h>
#include <UserTimer.h>
#include <util/AutoLock.h>
#include <vfs.h>
#include <vm/vm.h>
#include <wait_for_objects.h>

#include "syscall_numbers.h"


typedef struct generic_syscall generic_syscall;

struct generic_syscall : DoublyLinkedListLinkImpl<generic_syscall> {
	char				subsystem[B_FILE_NAME_LENGTH];
	syscall_hook		hook;
	uint32				version;
	uint32				flags;
	int32				use_count;
	bool				valid;
	ConditionVariable	unused_condition;
	generic_syscall*	previous;
};

typedef DoublyLinkedList<generic_syscall> GenericSyscallList;


static mutex sGenericSyscallLock = MUTEX_INITIALIZER("generic syscall");
static GenericSyscallList sGenericSyscalls;


status_t _user_generic_syscall(const char* userSubsystem, uint32 function,
	void* buffer, size_t bufferSize);
int _user_is_computer_on(void);


#if SYSCALL_TRACING
static int dump_syscall_tracing(int argc, char** argv);
#endif


static generic_syscall*
find_generic_syscall(const char* subsystem)
{
	ASSERT_LOCKED_MUTEX(&sGenericSyscallLock);

	GenericSyscallList::Iterator iterator = sGenericSyscalls.GetIterator();

	while (generic_syscall* syscall = iterator.Next()) {
		if (!strcmp(syscall->subsystem, subsystem))
			return syscall;
	}

	return NULL;
}


/**
 * @brief Syscall: invoke a registered generic syscall subsystem from user space.
 *
 * Looks up the subsystem named by @p userSubsystem and dispatches @p function
 * to its hook. Also handles the reserved B_SYSCALL_INFO function, which
 * returns the subsystem's current version number.
 *
 * Returns B_NAME_NOT_FOUND if either the subsystem was not found, or
 * the subsystem does not support the requested function.
 * All other return codes depend on the generic syscall implementation.
 *
 * @param userSubsystem User-space string naming the subsystem (e.g. "disk_device").
 * @param function      Function code to invoke within the subsystem.
 * @param buffer        In/out buffer whose meaning is defined by the subsystem.
 * @param bufferSize    Size of @p buffer in bytes.
 * @retval B_OK              The subsystem hook returned B_OK.
 * @retval B_BAD_ADDRESS     @p userSubsystem is not a valid user-space pointer.
 * @retval B_NAME_NOT_FOUND  No subsystem with that name is registered, or the
 *                           subsystem does not handle @p function.
 * @retval B_BAD_VALUE       B_SYSCALL_INFO was requested but @p bufferSize is wrong.
 * @retval B_BAD_TYPE        B_SYSCALL_INFO was requested but the caller's version
 *                           is older than the registered version.
 */
status_t
_user_generic_syscall(const char* userSubsystem, uint32 function,
	void* buffer, size_t bufferSize)
{
	char subsystem[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userSubsystem)
		|| user_strlcpy(subsystem, userSubsystem, sizeof(subsystem)) < B_OK)
		return B_BAD_ADDRESS;

	//dprintf("generic_syscall(subsystem = \"%s\", function = %lu)\n", subsystem, function);

	MutexLocker locker(sGenericSyscallLock);

	generic_syscall* syscall = find_generic_syscall(subsystem);
	if (syscall == NULL)
		return B_NAME_NOT_FOUND;

	if (function >= B_RESERVED_SYSCALL_BASE) {
		if (function != B_SYSCALL_INFO) {
			// this is all we know
			return B_NAME_NOT_FOUND;
		}

		// special info syscall
		if (bufferSize != sizeof(uint32))
			return B_BAD_VALUE;

		uint32 requestedVersion;

		// retrieve old version
		if (user_memcpy(&requestedVersion, buffer, sizeof(uint32)) != B_OK)
			return B_BAD_ADDRESS;
		if (requestedVersion != 0 && requestedVersion < syscall->version)
			return B_BAD_TYPE;

		// return current version
		return user_memcpy(buffer, &syscall->version, sizeof(uint32));
	}

	while (syscall != NULL) {
		generic_syscall* next;

		if (syscall->valid) {
			syscall->use_count++;
			locker.Unlock();

			status_t status
				= syscall->hook(subsystem, function, buffer, bufferSize);

			locker.Lock();

			if (--syscall->use_count == 0)
				syscall->unused_condition.NotifyAll();

			if (status != B_BAD_HANDLER)
				return status;
		}

		// the syscall may have been removed in the mean time
		next = find_generic_syscall(subsystem);
		if (next == syscall)
			syscall = syscall->previous;
		else
			syscall = next;
	}

	return B_NAME_NOT_FOUND;
}


int
_user_is_computer_on(void)
{
	return 1;
}


//	#pragma mark -


/**
 * @brief Central syscall dispatcher: maps a syscall index to its kernel handler.
 *
 * Called from the architecture-specific syscall entry path. Invokes the
 * pre- and post-syscall debug hooks, then uses a generated switch statement
 * (from syscall_dispatcher.h) to call the appropriate handler and store the
 * 64-bit return value.
 *
 * @param callIndex     The syscall number extracted from the trap frame.
 * @param args          Pointer to the packed syscall argument block.
 * @param _returnValue  Out-parameter; receives the handler's return value.
 * @return B_HANDLED_INTERRUPT always, so the interrupt handler knows the
 *         trap was consumed.
 */
int32
syscall_dispatcher(uint32 callIndex, void* args, uint64* _returnValue)
{
//	dprintf("syscall_dispatcher: thread 0x%x call 0x%x, arg0 0x%x, arg1 0x%x arg2 0x%x arg3 0x%x arg4 0x%x\n",
//		thread_get_current_thread_id(), call_num, arg0, arg1, arg2, arg3, arg4);

	user_debug_pre_syscall(callIndex, args);

	switch (callIndex) {
		// the cases are auto-generated
		#include "syscall_dispatcher.h"

		default:
			*_returnValue = (uint64)B_BAD_VALUE;
	}

	user_debug_post_syscall(callIndex, args, *_returnValue);

//	dprintf("syscall_dispatcher: done with syscall 0x%x\n", callIndex);

	return B_HANDLED_INTERRUPT;
}


status_t
generic_syscall_init(void)
{
	new(&sGenericSyscalls) GenericSyscallList;

#if	SYSCALL_TRACING
	add_debugger_command_etc("straced", &dump_syscall_tracing,
		"Dump recorded syscall trace entries",
		"Prints recorded trace entries. It is wrapper for the \"traced\"\n"
		"command and supports all of its command line options (though\n"
		"backward tracing doesn't really work). The difference is that if a\n"
		"pre syscall trace entry is encountered, the corresponding post\n"
		"syscall traced entry is also printed, even if it doesn't match the\n"
		"given filter.\n", 0);
#endif	// SYSCALL_TRACING

	return B_OK;
}


// #pragma mark - public API


/**
 * @brief Register a new generic syscall subsystem.
 *
 * Makes @p hook callable from user space via _user_generic_syscall() under
 * the name @p subsystem. If a handler for the subsystem already exists the
 * behaviour depends on @p flags:
 * - B_DO_NOT_REPLACE_SYSCALL: returns B_NAME_IN_USE without replacing.
 * - B_SYSCALL_NOT_REPLACEABLE on the existing entry: returns B_NOT_ALLOWED.
 * Otherwise the existing entry is superseded and the new one takes over (the
 * old one is saved as @c previous so it can be restored on unregister).
 *
 * @param subsystem Null-terminated name string for the subsystem.
 * @param hook      Handler function invoked for every call to this subsystem.
 * @param version   Version number advertised via B_SYSCALL_INFO.
 * @param flags     Combination of B_DO_NOT_REPLACE_SYSCALL and
 *                  B_SYSCALL_NOT_REPLACEABLE flags.
 * @retval B_OK           The subsystem was registered successfully.
 * @retval B_BAD_VALUE    @p hook is @c NULL.
 * @retval B_NAME_IN_USE  A handler already exists and replacement is disallowed.
 * @retval B_NOT_ALLOWED  The existing handler is marked not-replaceable.
 * @retval B_NO_MEMORY    Allocation of the internal record failed.
 */
status_t
register_generic_syscall(const char* subsystem, syscall_hook hook,
	uint32 version, uint32 flags)
{
	if (hook == NULL)
		return B_BAD_VALUE;

	MutexLocker _(sGenericSyscallLock);

	generic_syscall* previous = find_generic_syscall(subsystem);
	if (previous != NULL) {
		if ((flags & B_DO_NOT_REPLACE_SYSCALL) != 0
			|| version < previous->version) {
			return B_NAME_IN_USE;
		}
		if ((previous->flags & B_SYSCALL_NOT_REPLACEABLE) != 0)
			return B_NOT_ALLOWED;
	}

	generic_syscall* syscall = new(std::nothrow) generic_syscall;
	if (syscall == NULL)
		return B_NO_MEMORY;

	strlcpy(syscall->subsystem, subsystem, sizeof(syscall->subsystem));
	syscall->hook = hook;
	syscall->version = version;
	syscall->flags = flags;
	syscall->use_count = 0;
	syscall->valid = true;
	syscall->previous = previous;
	syscall->unused_condition.Init(syscall, "syscall unused");

	sGenericSyscalls.Add(syscall);

	if (previous != NULL)
		sGenericSyscalls.Remove(previous);

	return B_OK;
}


/**
 * @brief Unregister a previously registered generic syscall subsystem.
 *
 * Marks the subsystem invalid so no new calls are dispatched to it, then
 * waits (blocking) until all in-flight calls complete. Once quiesced, removes
 * the entry and reinstates the previous handler if one was displaced during
 * registration.
 *
 * @param subsystem Null-terminated name of the subsystem to remove.
 * @param version   Version of the handler to remove (currently unused; any
 *                  version matching the name is removed).
 * @retval B_OK              The subsystem was unregistered successfully.
 * @retval B_NAME_NOT_FOUND  No subsystem with that name is registered.
 */
status_t
unregister_generic_syscall(const char* subsystem, uint32 version)
{
	// TODO: we should only remove the syscall with the matching version

	while (true) {
		MutexLocker locker(sGenericSyscallLock);

		generic_syscall* syscall = find_generic_syscall(subsystem);
		if (syscall == NULL)
			return B_NAME_NOT_FOUND;

		syscall->valid = false;

		if (syscall->use_count != 0) {
			// Wait until the syscall isn't in use anymore
			ConditionVariableEntry entry;
			syscall->unused_condition.Add(&entry);

			locker.Unlock();

			entry.Wait();
			continue;
		}

		if (syscall->previous != NULL) {
			// reestablish the old syscall
			sGenericSyscalls.Add(syscall->previous);
		}

		sGenericSyscalls.Remove(syscall);
		delete syscall;

		return B_OK;
	}
}


// #pragma mark - syscall tracing


#if SYSCALL_TRACING

namespace SyscallTracing {


static const char*
get_syscall_name(uint32 syscall)
{
	if (syscall >= (uint32)kSyscallCount)
		return "<invalid syscall number>";

	return kExtendedSyscallInfos[syscall].name;
}


class PreSyscall : public AbstractTraceEntry {
	public:
		PreSyscall(uint32 syscall, const void* parameters)
			:
			fSyscall(syscall),
			fParameters(NULL)
		{
			if (syscall < (uint32)kSyscallCount) {
				fParameters = alloc_tracing_buffer_memcpy(parameters,
					kSyscallInfos[syscall].parameter_size, false);

				// copy string parameters, if any
				if (fParameters != NULL && syscall != SYSCALL_KTRACE_OUTPUT) {
					int32 stringIndex = 0;
					const extended_syscall_info& syscallInfo
						= kExtendedSyscallInfos[fSyscall];
					for (int i = 0; i < syscallInfo.parameter_count; i++) {
						const syscall_parameter_info& paramInfo
							= syscallInfo.parameters[i];
						if (paramInfo.type != B_STRING_TYPE)
							continue;

						const uint8* data
							= (uint8*)fParameters + paramInfo.offset;
						if (stringIndex < MAX_PARAM_STRINGS) {
							fParameterStrings[stringIndex++]
								= alloc_tracing_buffer_strcpy(
									*(const char**)data, 64, true);
						}
					}
				}
			}

			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("syscall pre:  %s(", get_syscall_name(fSyscall));

			if (fParameters != NULL) {
				int32 stringIndex = 0;
				const extended_syscall_info& syscallInfo
					= kExtendedSyscallInfos[fSyscall];
				for (int i = 0; i < syscallInfo.parameter_count; i++) {
					const syscall_parameter_info& paramInfo
						= syscallInfo.parameters[i];
					const uint8* data = (uint8*)fParameters + paramInfo.offset;
					uint64 value = 0;
					bool printValue = true;
					switch (paramInfo.type) {
						case B_INT8_TYPE:
							value = *(uint8*)data;
							break;
						case B_INT16_TYPE:
							value = *(uint16*)data;
							break;
						case B_INT32_TYPE:
							value = *(uint32*)data;
							break;
						case B_INT64_TYPE:
							value = *(uint64*)data;
							break;
						case B_POINTER_TYPE:
							value = (uint64)*(void**)data;
							break;
						case B_STRING_TYPE:
							if (stringIndex < MAX_PARAM_STRINGS
								&& fSyscall != SYSCALL_KTRACE_OUTPUT) {
								out.Print("%s\"%s\"",
									(i == 0 ? "" : ", "),
									fParameterStrings[stringIndex++]);
								printValue = false;
							} else
								value = (uint64)*(void**)data;
							break;
					}

					if (printValue)
						out.Print("%s%#" B_PRIx64, (i == 0 ? "" : ", "), value);
				}
			}

			out.Print(")");
		}

	private:
		enum { MAX_PARAM_STRINGS = 3 };

		uint32		fSyscall;
		void*		fParameters;
		const char*	fParameterStrings[MAX_PARAM_STRINGS];
};


class PostSyscall : public AbstractTraceEntry {
	public:
		PostSyscall(uint32 syscall, uint64 returnValue)
			:
			fSyscall(syscall),
			fReturnValue(returnValue)
		{
			Initialized();
#if 0
			if (syscall < (uint32)kSyscallCount
				&&  returnValue != (returnValue & 0xffffffff)
				&& kExtendedSyscallInfos[syscall].return_type.size <= 4) {
				panic("syscall return value 64 bit although it should be 32 "
					"bit");
			}
#endif
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("syscall post: %s() -> %#" B_PRIx64,
				get_syscall_name(fSyscall), fReturnValue);
		}

	private:
		uint32	fSyscall;
		uint64	fReturnValue;
};

}	// namespace SyscallTracing


extern "C" void trace_pre_syscall(uint32 syscallNumber, const void* parameters);

void
trace_pre_syscall(uint32 syscallNumber, const void* parameters)
{
#if SYSCALL_TRACING_IGNORE_KTRACE_OUTPUT
	if (syscallNumber != SYSCALL_KTRACE_OUTPUT)
#endif
	{
		new(std::nothrow) SyscallTracing::PreSyscall(syscallNumber, parameters);
	}
}


extern "C" void trace_post_syscall(int syscallNumber, uint64 returnValue);

void
trace_post_syscall(int syscallNumber, uint64 returnValue)
{
#if SYSCALL_TRACING_IGNORE_KTRACE_OUTPUT
	if (syscallNumber != SYSCALL_KTRACE_OUTPUT)
#endif
	{
		new(std::nothrow) SyscallTracing::PostSyscall(syscallNumber,
			returnValue);
	}
}


using namespace SyscallTracing;

class SyscallWrapperTraceFilter : public WrapperTraceFilter {
public:
	virtual void Init(TraceFilter* filter, int direction, bool continued)
	{
		fFilter = filter;
		fHitThreadLimit = false;
		fDirection = direction;

		if (!continued)
			fPendingThreadCount = 0;
	}

	virtual bool Filter(const TraceEntry* _entry, LazyTraceOutput& out)
	{
		if (fFilter == NULL)
			return true;

		if (fDirection < 0)
			return fFilter->Filter(_entry, out);

		if (const PreSyscall* entry = dynamic_cast<const PreSyscall*>(_entry)) {
			_RemovePendingThread(entry->ThreadID());

			bool accepted = fFilter->Filter(entry, out);
			if (accepted)
				_AddPendingThread(entry->ThreadID());
			return accepted;

		} else if (const PostSyscall* entry
				= dynamic_cast<const PostSyscall*>(_entry)) {
			bool wasPending = _RemovePendingThread(entry->ThreadID());

			return wasPending || fFilter->Filter(entry, out);

		} else if (const AbstractTraceEntry* entry
				= dynamic_cast<const AbstractTraceEntry*>(_entry)) {
			bool isPending = _IsPendingThread(entry->ThreadID());

			return isPending || fFilter->Filter(entry, out);

		} else {
			return fFilter->Filter(_entry, out);
		}
	}

	bool HitThreadLimit() const
	{
		return fHitThreadLimit;
	}

	int Direction() const
	{
		return fDirection;
	}

private:
	enum {
		MAX_PENDING_THREADS = 32
	};

	bool _AddPendingThread(thread_id thread)
	{
		int32 index = _PendingThreadIndex(thread);
		if (index >= 0)
			return true;

		if (fPendingThreadCount == MAX_PENDING_THREADS) {
			fHitThreadLimit = true;
			return false;
		}

		fPendingThreads[fPendingThreadCount++] = thread;
		return true;
	}

	bool _RemovePendingThread(thread_id thread)
	{
		int32 index = _PendingThreadIndex(thread);
		if (index < 0)
			return false;

		if (index + 1 < fPendingThreadCount) {
			memmove(fPendingThreads + index, fPendingThreads + index + 1,
				fPendingThreadCount - index - 1);
		}

		fPendingThreadCount--;
		return true;
	}

	bool _IsPendingThread(thread_id thread)
	{
		return _PendingThreadIndex(thread) >= 0;
	}

	int32 _PendingThreadIndex(thread_id thread)
	{
		for (int32 i = 0; i < fPendingThreadCount; i++) {
			if (fPendingThreads[i] == thread)
				return i;
		}
		return -1;
	}

	TraceFilter*	fFilter;
	thread_id		fPendingThreads[MAX_PENDING_THREADS];
	int32			fPendingThreadCount;
	int				fDirection;
	bool			fHitThreadLimit;
};


static SyscallWrapperTraceFilter sFilter;

static int
dump_syscall_tracing(int argc, char** argv)
{
	new(&sFilter) SyscallWrapperTraceFilter;
	int result = dump_tracing(argc, argv, &sFilter);

	if (sFilter.HitThreadLimit()) {
		kprintf("Warning: The thread buffer was too small to track all "
			"threads!\n");
	} else if (sFilter.HitThreadLimit()) {
		kprintf("Warning: Can't track syscalls backwards!\n");
	}

	return result;
}


#endif	// SYSCALL_TRACING


/*
 * kSyscallCount and kSyscallInfos here
 */
// generated by gensyscalls
#include "syscall_table.h"
