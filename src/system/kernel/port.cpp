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
 *   Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Mark-Jan Bastian. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file port.cpp
 * @brief Kernel message ports — unidirectional IPC with buffered message queues.
 *
 * Implements the Haiku port API (create_port, write_port_etc, read_port_etc,
 * delete_port, find_port, etc.). Ports are named, bounded message queues
 * owned by teams. Writing blocks when full; reading blocks when empty.
 * Ports are automatically deleted when their owning team exits.
 *
 * @see sem.cpp, team.cpp
 */


#include <port.h>

#include <algorithm>
#include <ctype.h>
#include <iovec.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>

#include <AutoDeleter.h>
#include <StackOrHeapArray.h>

#include <arch/int.h>
#include <heap.h>
#include <kernel.h>
#include <Notifications.h>
#include <sem.h>
#include <syscall_restart.h>
#include <team.h>
#include <tracing.h>
#include <util/AutoLock.h>
#include <util/list.h>
#include <util/iovec_support.h>
#include <vm/vm.h>
#include <wait_for_objects.h>


//#define TRACE_PORTS
#ifdef TRACE_PORTS
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


#if __GNUC__ >= 3
#	define GCC_2_NRV(x)
	// GCC >= 3.1 doesn't need it anymore
#else
#	define GCC_2_NRV(x) return x;
	// GCC 2 named return value syntax
	// see http://gcc.gnu.org/onlinedocs/gcc-2.95.2/gcc_5.html#SEC106
#endif


// Locking:
// * sPortsLock: Protects the sPorts and sPortsByName hash tables.
// * sTeamListLock[]: Protects Team::port_list. Lock index for given team is
//   (Team::id % kTeamListLockCount).
// * Port::lock: Protects all Port members save team_link, hash_link, lock and
//   state. id is immutable.
//
// Port::state ensures atomicity by providing a linearization point for adding
// and removing ports to the hash tables and the team port list.
// * sPortsLock and sTeamListLock[] are locked separately and not in a nested
//   fashion, so a port can be in the hash table but not in the team port list
//   or vice versa. => Without further provisions, insertion and removal are
//   not linearizable and thus not concurrency-safe.
// * To make insertion and removal linearizable, Port::state was added. It is
//   always only accessed atomically and updates are done using
//   atomic_test_and_set(). A port is only seen as existent when its state is
//   Port::kActive.
// * Deletion of ports is done in two steps: logical and physical deletion.
//   First, logical deletion happens and sets Port::state to Port::kDeleted.
//   This is an atomic operation and from then on, functions like
//   get_locked_port() consider this port as deleted and ignore it. Secondly,
//   physical deletion removes the port from hash tables and team port list.
//   In a similar way, port creation first inserts into hashes and team list
//   and only then sets port to Port::kActive.
//   This creates a linearization point at the atomic update of Port::state,
//   operations become linearizable and thus concurrency-safe. To help
//   understanding, the linearization points are annotated with comments.
// * Ports are reference-counted so it's not a problem when someone still
//   has a reference to a deleted port.


namespace {

struct port_message : DoublyLinkedListLinkImpl<port_message> {
	int32				code;
	size_t				size;
	uid_t				sender;
	gid_t				sender_group;
	team_id				sender_team;
	char				buffer[0];
};

typedef DoublyLinkedList<port_message> MessageList;

} // namespace


static void put_port_message(port_message* message);


namespace {

struct Port : public KernelReferenceable {
	enum State {
		kUnused = 0,
		kActive,
		kDeleted
	};

	struct list_link	team_link;
	Port*				hash_link;
	port_id				id;
	team_id				owner;
	Port*				name_hash_link;
	size_t				name_hash;
	int32				capacity;
	mutex				lock;
	int32				state;
	uint32				read_count;
	int32				write_count;
	ConditionVariable	read_condition;
	ConditionVariable	write_condition;
	int32				total_count;
		// messages read from port since creation
	select_info*		select_infos;
	MessageList			messages;

	Port(team_id owner, int32 queueLength, const char* name)
		:
		owner(owner),
		name_hash(0),
		capacity(queueLength),
		state(kUnused),
		read_count(0),
		write_count(queueLength),
		total_count(0),
		select_infos(NULL)
	{
		// id is initialized when the caller adds the port to the hash table

		mutex_init_etc(&lock, name, MUTEX_FLAG_CLONE_NAME);
		read_condition.Init(this, "port read");
		write_condition.Init(this, "port write");
	}

	virtual ~Port()
	{
		while (port_message* message = messages.RemoveHead())
			put_port_message(message);

		mutex_destroy(&lock);
	}
};


struct PortHashDefinition {
	typedef port_id		KeyType;
	typedef	Port		ValueType;

	size_t HashKey(port_id key) const
	{
		return key;
	}

	size_t Hash(Port* value) const
	{
		return HashKey(value->id);
	}

	bool Compare(port_id key, Port* value) const
	{
		return value->id == key;
	}

	Port*& GetLink(Port* value) const
	{
		return value->hash_link;
	}
};

typedef BOpenHashTable<PortHashDefinition> PortHashTable;


struct PortNameHashDefinition {
	typedef const char*	KeyType;
	typedef	Port		ValueType;

	size_t HashKey(const char* key) const
	{
		// Hash function: hash(key) =  key[0] * 31^(length - 1)
		//   + key[1] * 31^(length - 2) + ... + key[length - 1]

		const size_t length = strlen(key);

		size_t hash = 0;
		for (size_t index = 0; index < length; index++)
			hash = 31 * hash + key[index];

		return hash;
	}

	size_t Hash(Port* value) const
	{
		size_t& hash = value->name_hash;
		if (hash == 0)
			hash = HashKey(value->lock.name);
		return hash;
	}

	bool Compare(const char* key, Port* value) const
	{
		return (strcmp(key, value->lock.name) == 0);
	}

	Port*& GetLink(Port* value) const
	{
		return value->name_hash_link;
	}
};

typedef BOpenHashTable<PortNameHashDefinition> PortNameHashTable;


class PortNotificationService : public DefaultNotificationService {
public:
							PortNotificationService();

			void			Notify(uint32 opcode, port_id team);
};

} // namespace


// #pragma mark - tracing


#if PORT_TRACING
namespace PortTracing {

class Create : public AbstractTraceEntry {
public:
	Create(Port* port)
		:
		fID(port->id),
		fOwner(port->owner),
		fCapacity(port->capacity)
	{
		fName = alloc_tracing_buffer_strcpy(port->lock.name, B_OS_NAME_LENGTH,
			false);

		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("port %ld created, name \"%s\", owner %ld, capacity %ld",
			fID, fName, fOwner, fCapacity);
	}

private:
	port_id				fID;
	char*				fName;
	team_id				fOwner;
	int32		 		fCapacity;
};


class Delete : public AbstractTraceEntry {
public:
	Delete(Port* port)
		:
		fID(port->id)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("port %ld deleted", fID);
	}

private:
	port_id				fID;
};


class Read : public AbstractTraceEntry {
public:
	Read(const BReference<Port>& portRef, int32 code, ssize_t result)
		:
		fID(portRef->id),
		fReadCount(portRef->read_count),
		fWriteCount(portRef->write_count),
		fCode(code),
		fResult(result)
	{
		Initialized();
	}

	Read(port_id id, int32 readCount, int32 writeCount, int32 code,
		ssize_t result)
		:
		fID(id),
		fReadCount(readCount),
		fWriteCount(writeCount),
		fCode(code),
		fResult(result)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("port %ld read, read %ld, write %ld, code %lx: %ld",
			fID, fReadCount, fWriteCount, fCode, fResult);
	}

private:
	port_id				fID;
	int32				fReadCount;
	int32				fWriteCount;
	int32				fCode;
	ssize_t				fResult;
};


class Write : public AbstractTraceEntry {
public:
	Write(port_id id, int32 readCount, int32 writeCount, int32 code,
		size_t bufferSize, ssize_t result)
		:
		fID(id),
		fReadCount(readCount),
		fWriteCount(writeCount),
		fCode(code),
		fBufferSize(bufferSize),
		fResult(result)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("port %ld write, read %ld, write %ld, code %lx, size %ld: %ld",
			fID, fReadCount, fWriteCount, fCode, fBufferSize, fResult);
	}

private:
	port_id				fID;
	int32				fReadCount;
	int32				fWriteCount;
	int32				fCode;
	size_t				fBufferSize;
	ssize_t				fResult;
};


class Info : public AbstractTraceEntry {
public:
	Info(const BReference<Port>& portRef, int32 code, ssize_t result)
		:
		fID(portRef->id),
		fReadCount(portRef->read_count),
		fWriteCount(portRef->write_count),
		fCode(code),
		fResult(result)
	{
		Initialized();
	}

	Info(port_id id, int32 readCount, int32 writeCount, int32 code,
		ssize_t result)
		:
		fID(id),
		fReadCount(readCount),
		fWriteCount(writeCount),
		fCode(code),
		fResult(result)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("port %ld info, read %ld, write %ld, code %lx: %ld",
			fID, fReadCount, fWriteCount, fCode, fResult);
	}

private:
	port_id				fID;
	int32				fReadCount;
	int32				fWriteCount;
	int32				fCode;
	ssize_t				fResult;
};


class OwnerChange : public AbstractTraceEntry {
public:
	OwnerChange(Port* port, team_id newOwner, status_t status)
		:
		fID(port->id),
		fOldOwner(port->owner),
		fNewOwner(newOwner),
		fStatus(status)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("port %ld owner change from %ld to %ld: %s", fID, fOldOwner,
			fNewOwner, strerror(fStatus));
	}

private:
	port_id				fID;
	team_id				fOldOwner;
	team_id				fNewOwner;
	status_t	 		fStatus;
};

}	// namespace PortTracing

#	define T(x) new(std::nothrow) PortTracing::x;
#else
#	define T(x) ;
#endif


static const size_t kInitialPortBufferSize = 4 * 1024 * 1024;
static const size_t kTotalSpaceLimit = 64 * 1024 * 1024;
static const size_t kTeamSpaceLimit = 8 * 1024 * 1024;
static const size_t kBufferGrowRate = kInitialPortBufferSize;

#define MAX_QUEUE_LENGTH 4096
#define PORT_MAX_MESSAGE_SIZE (256 * 1024)

static int32 sMaxPorts = 4096;
static int32 sUsedPorts;

static PortHashTable sPorts;
static PortNameHashTable sPortsByName;
static ConditionVariable sNoSpaceCondition;
static int32 sTotalSpaceCommited;
static int32 sWaitingForSpace;
static port_id sNextPortID = 1;
static bool sPortsActive = false;
static rw_lock sPortsLock = RW_LOCK_INITIALIZER("ports list");

enum {
	kTeamListLockCount = 8
};

static mutex sTeamListLock[kTeamListLockCount] = {
	MUTEX_INITIALIZER("team ports list 1"),
	MUTEX_INITIALIZER("team ports list 2"),
	MUTEX_INITIALIZER("team ports list 3"),
	MUTEX_INITIALIZER("team ports list 4"),
	MUTEX_INITIALIZER("team ports list 5"),
	MUTEX_INITIALIZER("team ports list 6"),
	MUTEX_INITIALIZER("team ports list 7"),
	MUTEX_INITIALIZER("team ports list 8")
};

static PortNotificationService sNotificationService;


//	#pragma mark - TeamNotificationService


PortNotificationService::PortNotificationService()
	:
	DefaultNotificationService("ports")
{
}


/**
 * @brief Sends a port monitor notification event to all registered listeners.
 *
 * @param opcode  The notification opcode (e.g., PORT_ADDED, PORT_REMOVED).
 * @param port    The ID of the port that triggered the event.
 * @note  Called with no locks held; safe to invoke from any context.
 */
void
PortNotificationService::Notify(uint32 opcode, port_id port)
{
	char eventBuffer[128];
	KMessage event;
	event.SetTo(eventBuffer, sizeof(eventBuffer), PORT_MONITOR);
	event.AddInt32("event", opcode);
	event.AddInt32("port", port);

	DefaultNotificationService::Notify(event, opcode);
}


//	#pragma mark - debugger commands


/**
 * @brief Kernel debugger command: prints a table of all active ports.
 *
 * @param argc  Argument count from the debugger command parser.
 * @param argv  Argument vector; optional filters: "team <id>", "owner <id>",
 *              or "name <substring>".
 * @return Always returns 0.
 * @note  Runs in the kernel debugger context; no locking is performed.
 */
static int
dump_port_list(int argc, char** argv)
{
	const char* name = NULL;
	team_id owner = -1;

	if (argc > 2) {
		if (!strcmp(argv[1], "team") || !strcmp(argv[1], "owner"))
			owner = strtoul(argv[2], NULL, 0);
		else if (!strcmp(argv[1], "name"))
			name = argv[2];
	} else if (argc > 1)
		owner = strtoul(argv[1], NULL, 0);

	kprintf("port             id  cap  read-cnt  write-cnt   total   team  "
		"name\n");

	for (PortHashTable::Iterator it = sPorts.GetIterator();
		Port* port = it.Next();) {
		if ((owner != -1 && port->owner != owner)
			|| (name != NULL && strstr(port->lock.name, name) == NULL))
			continue;

		kprintf("%p %8" B_PRId32 " %4" B_PRId32 " %9" B_PRIu32 " %9" B_PRId32
			" %8" B_PRId32 " %6" B_PRId32 "  %s\n", port, port->id,
			port->capacity, port->read_count, port->write_count,
			port->total_count, port->owner, port->lock.name);
	}

	return 0;
}


/**
 * @brief Prints detailed information about a single port to the kernel debugger.
 *
 * @param port  Pointer to the Port structure to display.
 * @note  Runs in the kernel debugger context; sets debugger variables
 *        _port, _portID, and _owner for subsequent commands.
 */
static void
_dump_port_info(Port* port)
{
	kprintf("PORT: %p\n", port);
	kprintf(" id:              %" B_PRId32 "\n", port->id);
	kprintf(" name:            \"%s\"\n", port->lock.name);
	kprintf(" owner:           %" B_PRId32 "\n", port->owner);
	kprintf(" capacity:        %" B_PRId32 "\n", port->capacity);
	kprintf(" read_count:      %" B_PRIu32 "\n", port->read_count);
	kprintf(" write_count:     %" B_PRId32 "\n", port->write_count);
	kprintf(" total count:     %" B_PRId32 "\n", port->total_count);

	if (!port->messages.IsEmpty()) {
		kprintf("messages:\n");

		MessageList::Iterator iterator = port->messages.GetIterator();
		while (port_message* message = iterator.Next()) {
			kprintf(" %p  %08" B_PRIx32 "  %ld\n", message, message->code, message->size);
		}
	}

	set_debug_variable("_port", (addr_t)port);
	set_debug_variable("_portID", port->id);
	set_debug_variable("_owner", port->owner);
}


/**
 * @brief Kernel debugger command: prints detailed information for a specific port.
 *
 * @param argc  Argument count from the debugger command parser.
 * @param argv  Argument vector; accepts a numeric port ID, an address
 *              (prefixed with "address"), a name (prefixed with "name"), or a
 *              condition variable address (prefixed with "condition").
 * @return Always returns 0.
 * @note  Runs in the kernel debugger context; no locking is performed.
 */
static int
dump_port_info(int argc, char** argv)
{
	ConditionVariable* condition = NULL;
	const char* name = NULL;

	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	if (argc > 2) {
		if (!strcmp(argv[1], "address")) {
			_dump_port_info((Port*)parse_expression(argv[2]));
			return 0;
		} else if (!strcmp(argv[1], "condition"))
			condition = (ConditionVariable*)parse_expression(argv[2]);
		else if (!strcmp(argv[1], "name"))
			name = argv[2];
	} else if (parse_expression(argv[1]) > 0) {
		// if the argument looks like a number, treat it as such
		int32 num = parse_expression(argv[1]);
		Port* port = sPorts.Lookup(num);
		if (port == NULL || port->state != Port::kActive) {
			kprintf("port %" B_PRId32 " (%#" B_PRIx32 ") doesn't exist!\n",
				num, num);
			return 0;
		}
		_dump_port_info(port);
		return 0;
	} else
		name = argv[1];

	// walk through the ports list, trying to match name
	for (PortHashTable::Iterator it = sPorts.GetIterator();
		Port* port = it.Next();) {
		if ((name != NULL && port->lock.name != NULL
				&& !strcmp(name, port->lock.name))
			|| (condition != NULL && (&port->read_condition == condition
				|| &port->write_condition == condition))) {
			_dump_port_info(port);
			return 0;
		}
	}

	return 0;
}


// #pragma mark - internal helper functions


/**
 * @brief Fires select events on a port's registered select_info list.
 *
 * @param port    Pointer to the port whose select listeners are notified.
 * @param events  Bitmask of events to signal (e.g., B_EVENT_READ, B_EVENT_WRITE,
 *                B_EVENT_INVALID).
 * @note  The port's lock must be held by the caller before invoking this function.
 */
static void
notify_port_select_events(Port* port, uint16 events)
{
	if (port->select_infos)
		notify_select_events_list(port->select_infos, events);
}


/**
 * @brief Looks up a port by ID, locks it, and returns a reference to it.
 *
 * @param id  The port_id to look up.
 * @return    A BReference to the locked Port on success, or an empty
 *            BReference if the port does not exist or has been deleted.
 * @note  On success, the caller must unlock portRef->lock when done.
 *        Only ports in the kActive state are returned.
 */
static BReference<Port>
get_locked_port(port_id id) GCC_2_NRV(portRef)
{
#if __GNUC__ >= 3
	BReference<Port> portRef;
#endif
	{
		ReadLocker portsLocker(sPortsLock);
		portRef.SetTo(sPorts.Lookup(id));
	}

	if (portRef != NULL && portRef->state == Port::kActive) {
		if (mutex_lock(&portRef->lock) != B_OK)
			portRef.Unset();
	} else
		portRef.Unset();

	return portRef;
}


/**
 * @brief Looks up a port by ID and returns a reference without locking it.
 *
 * @param id  The port_id to look up.
 * @return    A BReference to the Port (in any state), or an empty BReference
 *            if the port is not found in the hash table.
 * @note  The returned port is not locked; the caller must synchronize access
 *        independently. Used when only a reference (not exclusive access) is needed.
 */
static BReference<Port>
get_port(port_id id) GCC_2_NRV(portRef)
{
#if __GNUC__ >= 3
	BReference<Port> portRef;
#endif
	ReadLocker portsLocker(sPortsLock);
	portRef.SetTo(sPorts.Lookup(id));

	return portRef;
}


/**
 * @brief Returns whether a port has been closed (no further writes allowed).
 *
 * @param port  Pointer to the Port to test.
 * @return      true if the port is closed (capacity == 0), false otherwise.
 * @note  The caller must hold the port's lock before calling this function.
 */
static inline bool
is_port_closed(Port* port)
{
	return port->capacity == 0;
}


/**
 * @brief Frees a port_message and returns its space to the global quota.
 *
 * @param message  Pointer to the port_message to free; must not be NULL.
 * @note  Decrements sTotalSpaceCommited atomically and wakes any threads
 *        blocked in get_port_message() waiting for quota to become available.
 */
static void
put_port_message(port_message* message)
{
	const size_t size = sizeof(port_message) + message->size;
	free(message);

	atomic_add(&sTotalSpaceCommited, -size);
	if (sWaitingForSpace > 0)
		sNoSpaceCondition.NotifyAll();
}


/**
 * @brief Allocates and initializes a port_message, blocking if the global
 *        message-buffer quota is exhausted.
 *
 * @param code        The integer message code to embed in the new message.
 * @param bufferSize  Size in bytes of the message payload buffer to allocate.
 * @param flags       Wait flags forwarded to ConditionVariableEntry::Wait()
 *                    (e.g., B_RELATIVE_TIMEOUT, B_ABSOLUTE_TIMEOUT).
 * @param timeout     Timeout value in microseconds used together with @p flags.
 * @param _message    On success, set to a pointer to the newly allocated
 *                    port_message; unchanged on failure.
 * @param port        Reference to the owning Port; used to re-validate the
 *                    port after blocking waits for quota space.
 * @retval B_OK           Message allocated successfully; *_message is valid.
 * @retval B_WOULD_BLOCK  Quota exhausted and B_RELATIVE_TIMEOUT with timeout <= 0.
 * @retval B_TIMED_OUT    Wait for quota space timed out.
 * @retval B_BAD_PORT_ID  The port was closed or deleted while waiting.
 * @note  Must be called with the port's lock held. The lock may be released
 *        and re-acquired internally while waiting for quota space.
 */
/*! Port must be locked. */
static status_t
get_port_message(int32 code, size_t bufferSize, uint32 flags, bigtime_t timeout,
	port_message** _message, Port& port)
{
	const size_t size = sizeof(port_message) + bufferSize;

	while (true) {
		int32 previouslyCommited = atomic_add(&sTotalSpaceCommited, size);

		while (previouslyCommited + size > kTotalSpaceLimit) {
			// TODO: add per team limit

			// We are not allowed to allocate more memory, as our
			// space limit has been reached - just wait until we get
			// some free space again.

			atomic_add(&sTotalSpaceCommited, -size);

			// TODO: we don't want to wait - but does that also mean we
			// shouldn't wait for free memory?
			if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0)
				return B_WOULD_BLOCK;

			ConditionVariableEntry entry;
			sNoSpaceCondition.Add(&entry);

			port_id portID = port.id;
			mutex_unlock(&port.lock);

			atomic_add(&sWaitingForSpace, 1);

			// TODO: right here the condition could be notified and we'd
			//       miss it.

			status_t status = entry.Wait(flags, timeout);

			atomic_add(&sWaitingForSpace, -1);

			// re-lock the port
			BReference<Port> newPortRef = get_locked_port(portID);

			if (newPortRef.Get() != &port || is_port_closed(&port)) {
				// the port is no longer usable
				return B_BAD_PORT_ID;
			}

			if (status == B_TIMED_OUT)
				return B_TIMED_OUT;

			previouslyCommited = atomic_add(&sTotalSpaceCommited, size);
			continue;
		}

		// Quota is fulfilled, try to allocate the buffer
		port_message* message = (port_message*)malloc(size);
		if (message != NULL) {
			message->code = code;
			message->size = bufferSize;

			*_message = message;
			return B_OK;
		}

		// We weren't able to allocate and we'll start over,so we remove our
		// size from the commited-counter again.
		atomic_add(&sTotalSpaceCommited, -size);
		continue;
	}
}


/**
 * @brief Fills a port_info structure with a snapshot of a port's current state.
 *
 * @param port  Pointer to the source Port; must not be NULL.
 * @param info  Pointer to the port_info structure to populate.
 * @param size  Size of the port_info structure (for future compatibility).
 * @note  The port's lock must be held by the caller before invoking this function.
 */
static void
fill_port_info(Port* port, port_info* info, size_t size)
{
	info->port = port->id;
	info->team = port->owner;
	info->capacity = port->capacity;

	info->queue_count = port->read_count;
	info->total_count = port->total_count;

	strlcpy(info->name, port->lock.name, B_OS_NAME_LENGTH);
}


/**
 * @brief Copies the payload of a port_message into the caller's buffer.
 *
 * @param message     Pointer to the source port_message.
 * @param _code       If non-NULL, receives the message's integer code.
 * @param buffer      Destination buffer for the message payload.
 * @param bufferSize  Size of @p buffer in bytes; copy is truncated if smaller
 *                    than the message payload.
 * @param userCopy    If true, uses user_memcpy() (safe for user-space addresses);
 *                    otherwise uses memcpy().
 * @return            Number of bytes actually copied on success, or a negative
 *                    error code if user_memcpy() fails.
 */
static ssize_t
copy_port_message(port_message* message, int32* _code, void* buffer,
	size_t bufferSize, bool userCopy)
{
	// check output buffer size
	size_t size = std::min(bufferSize, message->size);

	// copy message
	if (_code != NULL)
		*_code = message->code;

	if (size > 0) {
		if (userCopy) {
			status_t status = user_memcpy(buffer, message->buffer, size);
			if (status != B_OK)
				return status;
		} else
			memcpy(buffer, message->buffer, size);
	}

	return size;
}


/**
 * @brief Wakes all threads waiting on a port and fires B_EVENT_INVALID select events.
 *
 * @param port  Pointer to the Port to uninitialize; must be locked by the caller.
 * @note  This is the final teardown step performed after a port has been logically
 *        and physically deleted. After this call, all blocked read_port() and
 *        write_port() callers will receive B_BAD_PORT_ID. Fires PORT_REMOVED
 *        notification to the port monitor service.
 */
static void
uninit_port(Port* port)
{
	MutexLocker locker(port->lock);

	notify_port_select_events(port, B_EVENT_INVALID);
	port->select_infos = NULL;

	// Release the threads that were blocking on this port.
	// read_port() will see the B_BAD_PORT_ID return value, and act accordingly
	port->read_condition.NotifyAll(B_BAD_PORT_ID);
	port->write_condition.NotifyAll(B_BAD_PORT_ID);
	sNotificationService.Notify(PORT_REMOVED, port->id);
}


/**
 * @brief Atomically marks a port as logically deleted (kDeleted state).
 *
 * @param port  Pointer to the Port to logically delete; the caller must hold
 *              at least one reference to ensure the Port object remains valid.
 * @retval B_OK           The port was successfully transitioned to kDeleted.
 * @retval B_BAD_PORT_ID  The port was already in the kDeleted state.
 * @note  This is the linearization point for port deletion. Physical removal
 *        from hash tables and the team port list must follow separately.
 *        Spins briefly if the port is still in kUnused state (being created).
 */
/*! Caller must ensure there is still a reference to the port. (Either by
 *  holding a reference itself or by holding a lock on one of the data
 *  structures in which it is referenced.)
 */
static status_t
delete_port_logical(Port* port)
{
	for (;;) {
		// Try to logically delete
		const int32 oldState = atomic_test_and_set(&port->state,
			Port::kDeleted, Port::kActive);
			// Linearization point for port deletion

		switch (oldState) {
			case Port::kActive:
				// Logical deletion succesful
				return B_OK;

			case Port::kDeleted:
				// Someone else already deleted it in the meantime
				TRACE(("delete_port_logical: already deleted port_id %ld\n",
						port->id));
				return B_BAD_PORT_ID;

			case Port::kUnused:
				// Port is still being created, retry
				continue;

			default:
				// Port state got corrupted somehow
				panic("Invalid port state!\n");
		}
	}
}


//	#pragma mark - private kernel API


/**
 * @brief Deletes all ports owned by a team; called when the team exits.
 *
 * @param team  Pointer to the Team whose ports are to be deleted.
 * @note  Iterates the team's port_list, logically deletes each port, removes
 *        all entries from the global hash tables, then calls uninit_port() on
 *        each to wake blocked threads and fire removal notifications.
 *        This function acquires and releases sTeamListLock and sPortsLock
 *        internally; it must not be called with either lock already held.
 */
/*! This function deletes all the ports that are owned by the passed team.
*/
void
delete_owned_ports(Team* team)
{
	TRACE(("delete_owned_ports(owner = %ld)\n", team->id));

	list deletionList;
	list_init_etc(&deletionList, port_team_link_offset());

	const uint8 lockIndex = team->id % kTeamListLockCount;
	MutexLocker teamPortsListLocker(sTeamListLock[lockIndex]);

	// Try to logically delete all ports from the team's port list.
	// On success, move the port to deletionList.
	Port* port = (Port*)list_get_first_item(&team->port_list);
	while (port != NULL) {
		status_t status = delete_port_logical(port);
			// Contains linearization point

		Port* nextPort = (Port*)list_get_next_item(&team->port_list, port);

		if (status == B_OK) {
			list_remove_link(&port->team_link);
			list_add_item(&deletionList, port);
		}

		port = nextPort;
	}

	teamPortsListLocker.Unlock();

	// Remove all ports in deletionList from hashes
	{
		WriteLocker portsLocker(sPortsLock);

		for (Port* port = (Port*)list_get_first_item(&deletionList);
			 port != NULL;
			 port = (Port*)list_get_next_item(&deletionList, port)) {

			sPorts.Remove(port);
			sPortsByName.Remove(port);
			port->ReleaseReference();
				// joint reference for sPorts and sPortsByName
		}
	}

	// Uninitialize ports and release team port list references
	while (Port* port = (Port*)list_remove_head_item(&deletionList)) {
		atomic_add(&sUsedPorts, -1);
		uninit_port(port);
		port->ReleaseReference();
			// Reference for team port list
	}
}


/**
 * @brief Returns the configured maximum number of ports allowed system-wide.
 *
 * @return The value of sMaxPorts (default 4096).
 */
int32
port_max_ports(void)
{
	return sMaxPorts;
}


/**
 * @brief Returns the current number of ports in use system-wide.
 *
 * @return The value of sUsedPorts.
 */
int32
port_used_ports(void)
{
	return sUsedPorts;
}


/**
 * @brief Returns the byte offset of the team_link field within a Port struct.
 *
 * @return Byte offset used when walking team port lists via list_get_next_item().
 * @note  Uses a NULL-pointer cast workaround because offsetof() cannot be used
 *        on a class with a vtable in older GCC versions.
 */
size_t
port_team_link_offset()
{
	// Somewhat ugly workaround since we cannot use offsetof() for a class
	// with vtable (GCC4 throws a warning then).
	Port* port = (Port*)0;
	return (size_t)&port->team_link;
}


/**
 * @brief Initializes the kernel port subsystem.
 *
 * @param args  Pointer to the kernel_args structure (unused by this function).
 * @retval B_OK       Initialization succeeded.
 * @retval B_NO_MEMORY Failed to initialize the port hash table or name hash table.
 * @note  Must be called once during kernel startup before any port operations.
 *        Registers the "ports" and "port" debugger commands and the port
 *        notification service.
 */
status_t
port_init(kernel_args *args)
{
	// initialize ports table and by-name hash
	new(&sPorts) PortHashTable;
	if (sPorts.Init() != B_OK) {
		panic("Failed to init port hash table!");
		return B_NO_MEMORY;
	}

	new(&sPortsByName) PortNameHashTable;
	if (sPortsByName.Init() != B_OK) {
		panic("Failed to init port by name hash table!");
		return B_NO_MEMORY;
	}

	sNoSpaceCondition.Init(&sPorts, "port space");

	// add debugger commands
	add_debugger_command_etc("ports", &dump_port_list,
		"Dump a list of all active ports (for team, with name, etc.)",
		"[ ([ \"team\" | \"owner\" ] <team>) | (\"name\" <name>) ]\n"
		"Prints a list of all active ports meeting the given\n"
		"requirement. If no argument is given, all ports are listed.\n"
		"  <team>             - The team owning the ports.\n"
		"  <name>             - Part of the name of the ports.\n", 0);
	add_debugger_command_etc("port", &dump_port_info,
		"Dump info about a particular port",
		"(<id> | [ \"address\" ] <address>) | ([ \"name\" ] <name>) "
			"| (\"condition\" <address>)\n"
		"Prints info about the specified port.\n"
		"  <address>   - Pointer to the port structure.\n"
		"  <name>      - Name of the port.\n"
		"  <condition> - address of the port's read or write condition.\n", 0);

	new(&sNotificationService) PortNotificationService();
	sNotificationService.Register();
	sPortsActive = true;
	return B_OK;
}


//	#pragma mark - public kernel API


/**
 * @brief Creates a new named message port with a bounded message queue.
 *
 * @param queueLength  Maximum number of messages the port can hold at once;
 *                     must be in the range [1, MAX_QUEUE_LENGTH].
 * @param name         Human-readable name for the port; copied internally.
 *                     If NULL, the port is named "unnamed port".
 * @return             A valid port_id (>= 0) on success, or a negative error code.
 * @retval B_BAD_VALUE    @p queueLength is out of range.
 * @retval B_NO_MEMORY    Allocation of the Port structure failed.
 * @retval B_NO_MORE_PORTS The system-wide port limit has been reached.
 * @retval B_BAD_TEAM_ID  The calling thread has no associated team.
 * @note  The new port is owned by the calling thread's team. Port creation is
 *        linearized via an atomic state transition from kUnused to kActive.
 */
port_id
create_port(int32 queueLength, const char* name)
{
	TRACE(("create_port(queueLength = %ld, name = \"%s\")\n", queueLength,
		name));

	if (!sPortsActive) {
		panic("ports used too early!\n");
		return B_BAD_PORT_ID;
	}
	if (queueLength < 1 || queueLength > MAX_QUEUE_LENGTH)
		return B_BAD_VALUE;

	Team* team = thread_get_current_thread()->team;
	if (team == NULL)
		return B_BAD_TEAM_ID;

	// create a port
	BReference<Port> port;
	{
		Port* newPort = new(std::nothrow) Port(team_get_current_team_id(),
			queueLength, name != NULL ? name : "unnamed port");
		if (newPort == NULL)
			return B_NO_MEMORY;
		port.SetTo(newPort, true);
	}

	// check the ports limit
	const int32 previouslyUsed = atomic_add(&sUsedPorts, 1);
	if (previouslyUsed + 1 >= sMaxPorts) {
		atomic_add(&sUsedPorts, -1);
		return B_NO_MORE_PORTS;
	}

	{
		WriteLocker locker(sPortsLock);

		// allocate a port ID
		do {
			port->id = sNextPortID++;

			// handle integer overflow
			if (sNextPortID < 0)
				sNextPortID = 1;
		} while (sPorts.Lookup(port->id) != NULL);

		// Insert port physically:
		// (1/2) Insert into hash tables
		port->AcquireReference();
			// joint reference for sPorts and sPortsByName

		sPorts.Insert(port);
		sPortsByName.Insert(port);
	}

	// (2/2) Insert into team list
	{
		const uint8 lockIndex = port->owner % kTeamListLockCount;
		MutexLocker teamPortsListLocker(sTeamListLock[lockIndex]);
		port->AcquireReference();
		list_add_item(&team->port_list, port);
	}

	// tracing, notifications, etc.
	T(Create(port));

	const port_id id = port->id;

	// Insert port logically by marking it active
	const int32 oldState = atomic_test_and_set(&port->state,
		Port::kActive, Port::kUnused);
		// Linearization point for port creation

	if (oldState != Port::kUnused) {
		// Nobody is allowed to tamper with the port before it's active.
		panic("Port state was modified during creation!\n");
	}

	TRACE(("create_port() done: port created %ld\n", id));

	sNotificationService.Notify(PORT_ADDED, id);
	return id;
}


/**
 * @brief Closes a port, preventing any further messages from being written to it.
 *
 * @param id  The port_id of the port to close.
 * @retval B_OK           The port was successfully closed.
 * @retval B_BAD_PORT_ID  The port does not exist or the subsystem is inactive.
 * @note  After closing, existing queued messages can still be read. All threads
 *        currently blocked in read_port() or write_port() on this port are
 *        woken with B_BAD_PORT_ID. Registered select() events are fired with
 *        B_EVENT_INVALID.
 */
status_t
close_port(port_id id)
{
	TRACE(("close_port(id = %ld)\n", id));

	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL) {
		TRACE(("close_port: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}
	MutexLocker lock(&portRef->lock, true);

	// mark port to disable writing - deleting the semaphores will
	// wake up waiting read/writes
	portRef->capacity = 0;

	notify_port_select_events(portRef, B_EVENT_INVALID);
	portRef->select_infos = NULL;

	portRef->read_condition.NotifyAll(B_BAD_PORT_ID);
	portRef->write_condition.NotifyAll(B_BAD_PORT_ID);

	return B_OK;
}


/**
 * @brief Deletes a port and releases all associated resources.
 *
 * @param id  The port_id of the port to delete.
 * @retval B_OK           The port was successfully deleted.
 * @retval B_BAD_PORT_ID  The port does not exist, was already deleted, or the
 *                        subsystem is inactive.
 * @note  Port deletion proceeds in two phases: first a logical delete
 *        (atomic state transition to kDeleted), then physical removal from
 *        the global hash tables and the owning team's port list. All threads
 *        blocked on the port are woken via uninit_port(). A PORT_REMOVED
 *        notification is sent to the port monitor service.
 */
status_t
delete_port(port_id id)
{
	TRACE(("delete_port(id = %ld)\n", id));

	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	BReference<Port> portRef = get_port(id);

	if (portRef == NULL) {
		TRACE(("delete_port: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}

	status_t status = delete_port_logical(portRef);
		// Contains linearization point
	if (status != B_OK)
		return status;

	// Now remove port physically:
	// (1/2) Remove from hash tables
	{
		WriteLocker portsLocker(sPortsLock);

		sPorts.Remove(portRef);
		sPortsByName.Remove(portRef);

		portRef->ReleaseReference();
			// joint reference for sPorts and sPortsByName
	}

	// (2/2) Remove from team port list
	{
		const uint8 lockIndex = portRef->owner % kTeamListLockCount;
		MutexLocker teamPortsListLocker(sTeamListLock[lockIndex]);

		list_remove_link(&portRef->team_link);
		portRef->ReleaseReference();
	}

	uninit_port(portRef);

	T(Delete(portRef));

	atomic_add(&sUsedPorts, -1);

	return B_OK;
}


/**
 * @brief Registers a select_info structure to receive events on a port.
 *
 * @param id    The port_id to watch.
 * @param info  Pointer to the select_info structure describing the events of
 *              interest (B_EVENT_READ, B_EVENT_WRITE, B_EVENT_INVALID).
 * @param kernel  If false, access to kernel-owned ports is denied.
 * @retval B_OK           Registration succeeded; pending events notified immediately.
 * @retval B_BAD_PORT_ID  The port does not exist or has already been closed.
 * @retval B_NOT_ALLOWED  A user-space caller attempted to select on a kernel port.
 * @note  The port is locked during registration. If the relevant events are
 *        already pending at registration time, notify_select_events() is called
 *        immediately.
 */
status_t
select_port(int32 id, struct select_info* info, bool kernel)
{
	if (id < 0)
		return B_BAD_PORT_ID;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(portRef->lock, true);

	// port must not yet be closed
	if (is_port_closed(portRef))
		return B_BAD_PORT_ID;

	if (!kernel && portRef->owner == team_get_kernel_team_id()) {
		// kernel port, but call from userland
		return B_NOT_ALLOWED;
	}

	info->selected_events &= B_EVENT_READ | B_EVENT_WRITE | B_EVENT_INVALID;

	if (info->selected_events != 0) {
		uint16 events = 0;

		info->next = portRef->select_infos;
		portRef->select_infos = info;

		// check for events
		if ((info->selected_events & B_EVENT_READ) != 0
			&& !portRef->messages.IsEmpty()) {
			events |= B_EVENT_READ;
		}

		if (portRef->write_count > 0)
			events |= B_EVENT_WRITE;

		if (events != 0)
			notify_select_events(info, events);
	}

	return B_OK;
}


/**
 * @brief Deregisters a select_info structure from a port.
 *
 * @param id    The port_id from which to deregister.
 * @param info  Pointer to the select_info structure to remove from the port's
 *              linked list; matched by pointer identity.
 * @param kernel  Reserved; not used by the current implementation.
 * @retval B_OK           Deregistration succeeded (or info was not registered).
 * @retval B_BAD_PORT_ID  The port does not exist.
 * @note  If info->selected_events is zero the function returns B_OK immediately
 *        without touching the port.
 */
status_t
deselect_port(int32 id, struct select_info* info, bool kernel)
{
	if (id < 0)
		return B_BAD_PORT_ID;
	if (info->selected_events == 0)
		return B_OK;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(portRef->lock, true);

	// find and remove the infos
	select_info** infoLocation = &portRef->select_infos;
	while (*infoLocation != NULL && *infoLocation != info)
		infoLocation = &(*infoLocation)->next;

	if (*infoLocation == info)
		*infoLocation = info->next;

	return B_OK;
}


/**
 * @brief Looks up a port by name and returns its port_id.
 *
 * @param name  The exact name of the port to find; must not be NULL.
 * @return      The port_id of the matching active port on success.
 * @retval B_NAME_NOT_FOUND  No active port with the given name exists.
 * @retval B_BAD_VALUE       @p name is NULL.
 * @note  Holds sPortsLock (read) for the duration of the hash lookup.
 *        Only ports in the kActive state are returned.
 */
port_id
find_port(const char* name)
{
	TRACE(("find_port(name = \"%s\")\n", name));

	if (!sPortsActive) {
		panic("ports used too early!\n");
		return B_NAME_NOT_FOUND;
	}
	if (name == NULL)
		return B_BAD_VALUE;

	ReadLocker locker(sPortsLock);
	Port* port = sPortsByName.Lookup(name);
		// Since we have sPortsLock and don't return the port itself,
		// no BReference necessary

	if (port != NULL && port->state == Port::kActive)
		return port->id;

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Fills a port_info structure with information about the specified port.
 *
 * @param id    The port_id of the port to query.
 * @param info  Pointer to the port_info structure to populate; must not be NULL.
 * @param size  Must equal sizeof(port_info); used for structure-size validation.
 * @retval B_OK           Success; @p info has been populated.
 * @retval B_BAD_VALUE    @p info is NULL or @p size does not match sizeof(port_info).
 * @retval B_BAD_PORT_ID  The port does not exist or the subsystem is inactive.
 * @note  Acquires the port's lock internally; safe to call from any context.
 */
status_t
_get_port_info(port_id id, port_info* info, size_t size)
{
	TRACE(("get_port_info(id = %ld)\n", id));

	if (info == NULL || size != sizeof(port_info))
		return B_BAD_VALUE;
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL) {
		TRACE(("get_port_info: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}
	MutexLocker locker(portRef->lock, true);

	// fill a port_info struct with info
	fill_port_info(portRef, info, size);
	return B_OK;
}


/**
 * @brief Iterates over the ports owned by a team and fills a port_info struct.
 *
 * @param teamID   The team whose ports are being enumerated.
 * @param _cookie  In/out iteration cookie; pass 0 for the first call and
 *                 preserve the updated value for subsequent calls.
 * @param info     Pointer to the port_info structure to populate.
 * @param size     Must equal sizeof(port_info).
 * @retval B_OK           Success; @p info populated and @p *_cookie advanced.
 * @retval B_BAD_VALUE    Invalid arguments (@p info, @p _cookie, or @p size).
 * @retval B_BAD_PORT_ID  No more ports available for the given team.
 * @retval B_BAD_TEAM_ID  The specified team does not exist.
 * @note  Closed ports are skipped during iteration. Acquires sTeamListLock
 *        and then the individual port lock internally.
 */
status_t
_get_next_port_info(team_id teamID, int32* _cookie, struct port_info* info,
	size_t size)
{
	TRACE(("get_next_port_info(team = %ld)\n", teamID));

	if (info == NULL || size != sizeof(port_info) || _cookie == NULL
		|| teamID < 0) {
		return B_BAD_VALUE;
	}
	if (!sPortsActive)
		return B_BAD_PORT_ID;

	Team* team = Team::Get(teamID);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	// iterate through the team's port list
	const uint8 lockIndex = teamID % kTeamListLockCount;
	MutexLocker teamPortsListLocker(sTeamListLock[lockIndex]);

	int32 stopIndex = *_cookie;
	int32 index = 0;

	Port* port = (Port*)list_get_first_item(&team->port_list);
	while (port != NULL) {
		if (!is_port_closed(port)) {
			if (index == stopIndex)
				break;
			index++;
		}

		port = (Port*)list_get_next_item(&team->port_list, port);
	}

	if (port == NULL)
		return B_BAD_PORT_ID;

	// fill in the port info
	BReference<Port> portRef = port;
	teamPortsListLocker.Unlock();
		// Only use portRef below this line...

	MutexLocker locker(portRef->lock);
	fill_port_info(portRef, info, size);

	*_cookie = stopIndex + 1;
	return B_OK;
}


/**
 * @brief Returns the size of the next queued message without consuming it.
 *
 * @param id  The port_id to query.
 * @return    Size in bytes of the next message's payload, or a negative error code.
 * @note      Equivalent to port_buffer_size_etc(id, 0, 0); blocks indefinitely
 *            if the port is empty.
 */
ssize_t
port_buffer_size(port_id id)
{
	return port_buffer_size_etc(id, 0, 0);
}


/**
 * @brief Returns the size of the next queued message with optional timeout.
 *
 * @param id       The port_id to query.
 * @param flags    Wait flags (e.g., B_RELATIVE_TIMEOUT, B_ABSOLUTE_TIMEOUT,
 *                 B_CAN_INTERRUPT).
 * @param timeout  Timeout in microseconds; interpreted according to @p flags.
 * @return         Size in bytes of the next message's payload, or a negative
 *                 error code (e.g., B_TIMED_OUT, B_BAD_PORT_ID).
 * @note  Delegates to _get_port_message_info_etc() and returns only the size field.
 */
ssize_t
port_buffer_size_etc(port_id id, uint32 flags, bigtime_t timeout)
{
	port_message_info info;
	status_t error = get_port_message_info_etc(id, &info, flags, timeout);
	return error != B_OK ? error : info.size;
}


/**
 * @brief Returns extended message info (size, sender credentials) for the
 *        next queued message, blocking if the port is empty.
 *
 * @param id        The port_id to query.
 * @param info      Pointer to a port_message_info structure to populate with
 *                  the message's size, sender UID, GID, and team ID.
 * @param infoSize  Must equal sizeof(port_message_info).
 * @param flags     Wait flags (e.g., B_RELATIVE_TIMEOUT, B_ABSOLUTE_TIMEOUT,
 *                  B_CAN_INTERRUPT, B_KILL_CAN_INTERRUPT).
 * @param timeout   Timeout in microseconds; interpreted according to @p flags.
 * @retval B_OK           Success; @p info has been populated.
 * @retval B_BAD_VALUE    @p info is NULL or @p infoSize is wrong.
 * @retval B_BAD_PORT_ID  The port does not exist or is closed with no messages.
 * @retval B_WOULD_BLOCK  Port is empty and B_RELATIVE_TIMEOUT with timeout <= 0.
 * @retval B_TIMED_OUT    The wait timed out.
 * @note  The message is not consumed; read_port_etc() must be called to dequeue it.
 *        Fires a read_condition notification to re-wake other waiting readers.
 */
status_t
_get_port_message_info_etc(port_id id, port_message_info* info,
	size_t infoSize, uint32 flags, bigtime_t timeout)
{
	if (info == NULL || infoSize != sizeof(port_message_info))
		return B_BAD_VALUE;
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	flags &= B_CAN_INTERRUPT | B_KILL_CAN_INTERRUPT | B_RELATIVE_TIMEOUT
		| B_ABSOLUTE_TIMEOUT;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(portRef->lock, true);

	if (is_port_closed(portRef) && portRef->messages.IsEmpty()) {
		T(Info(portRef, 0, B_BAD_PORT_ID));
		TRACE(("_get_port_message_info_etc(): closed port %ld\n", id));
		return B_BAD_PORT_ID;
	}

	while (portRef->read_count == 0) {
		// We need to wait for a message to appear
		if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0)
			return B_WOULD_BLOCK;

		ConditionVariableEntry entry;
		portRef->read_condition.Add(&entry);

		locker.Unlock();

		// block if no message, or, if B_TIMEOUT flag set, block with timeout
		status_t status = entry.Wait(flags, timeout);

		if (status != B_OK) {
			T(Info(portRef, 0, status));
			return status;
		}

		// re-lock
		BReference<Port> newPortRef = get_locked_port(id);
		if (newPortRef == NULL) {
			T(Info(id, 0, 0, 0, B_BAD_PORT_ID));
			return B_BAD_PORT_ID;
		}
		locker.SetTo(newPortRef->lock, true);

		if (newPortRef != portRef
			|| (is_port_closed(portRef) && portRef->messages.IsEmpty())) {
			// the port is no longer there
			T(Info(id, 0, 0, 0, B_BAD_PORT_ID));
			return B_BAD_PORT_ID;
		}
	}

	// determine tail & get the length of the message
	port_message* message = portRef->messages.Head();
	if (message == NULL) {
		panic("port %" B_PRId32 ": no messages found\n", portRef->id);
		return B_ERROR;
	}

	info->size = message->size;
	info->sender = message->sender;
	info->sender_group = message->sender_group;
	info->sender_team = message->sender_team;

	T(Info(portRef, message->code, B_OK));

	// notify next one, as we haven't read from the port
	portRef->read_condition.NotifyOne();

	return B_OK;
}


/**
 * @brief Returns the number of messages currently queued in a port.
 *
 * @param id  The port_id to query.
 * @return    The number of queued messages (>= 0) on success, or a negative
 *            error code (B_BAD_PORT_ID) if the port is invalid.
 * @note  Acquires the port's lock briefly; the count may change immediately
 *        after the lock is released.
 */
ssize_t
port_count(port_id id)
{
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL) {
		TRACE(("port_count: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}
	MutexLocker locker(portRef->lock, true);

	// return count of messages
	return portRef->read_count;
}


/**
 * @brief Dequeues one message from a port, blocking indefinitely if empty.
 *
 * @param port        The port_id to read from.
 * @param msgCode     Receives the integer message code of the dequeued message.
 * @param buffer      Destination buffer for the message payload.
 * @param bufferSize  Size in bytes of @p buffer.
 * @return            Number of bytes copied into @p buffer on success, or a
 *                    negative error code.
 * @note  Equivalent to read_port_etc(port, msgCode, buffer, bufferSize, 0, 0).
 */
ssize_t
read_port(port_id port, int32* msgCode, void* buffer, size_t bufferSize)
{
	return read_port_etc(port, msgCode, buffer, bufferSize, 0, 0);
}


/**
 * @brief Dequeues one message from a port with optional flags and timeout.
 *
 * @param id          The port_id to read from.
 * @param _code       If non-NULL, receives the integer message code.
 * @param buffer      Destination buffer for the message payload.
 * @param bufferSize  Size in bytes of @p buffer; payload is truncated if larger.
 * @param flags       Wait flags and behavior modifiers:
 *                    - B_CAN_INTERRUPT / B_KILL_CAN_INTERRUPT
 *                    - B_RELATIVE_TIMEOUT / B_ABSOLUTE_TIMEOUT
 *                    - PORT_FLAG_USE_USER_MEMCPY (internal: use user_memcpy)
 *                    - B_PEEK_PORT_MESSAGE (internal: peek without consuming)
 * @param timeout     Timeout in microseconds; interpreted according to @p flags.
 * @return            Number of bytes copied into @p buffer on success, or a
 *                    negative error code.
 * @retval B_BAD_PORT_ID  The port does not exist or is closed with no messages.
 * @retval B_BAD_VALUE    @p buffer is NULL but @p bufferSize > 0, or timeout < 0.
 * @retval B_WOULD_BLOCK  Port is empty and B_RELATIVE_TIMEOUT with timeout <= 0.
 * @retval B_TIMED_OUT    The wait timed out.
 * @note  Blocks if the port is empty until a message arrives or the timeout
 *        expires. After dequeuing, one write slot is freed and a waiting writer
 *        is notified. B_EVENT_WRITE is fired on registered select_info entries.
 */
ssize_t
read_port_etc(port_id id, int32* _code, void* buffer, size_t bufferSize,
	uint32 flags, bigtime_t timeout)
{
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;
	if ((buffer == NULL && bufferSize > 0) || timeout < 0)
		return B_BAD_VALUE;

	bool userCopy = (flags & PORT_FLAG_USE_USER_MEMCPY) != 0;
	bool peekOnly = !userCopy && (flags & B_PEEK_PORT_MESSAGE) != 0;
		// TODO: we could allow peeking for user apps now

	flags &= B_CAN_INTERRUPT | B_KILL_CAN_INTERRUPT | B_RELATIVE_TIMEOUT
		| B_ABSOLUTE_TIMEOUT;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL)
		return B_BAD_PORT_ID;
	MutexLocker locker(portRef->lock, true);

	if (is_port_closed(portRef) && portRef->messages.IsEmpty()) {
		T(Read(portRef, 0, B_BAD_PORT_ID));
		TRACE(("read_port_etc(): closed port %ld\n", id));
		return B_BAD_PORT_ID;
	}

	while (portRef->read_count == 0) {
		if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0)
			return B_WOULD_BLOCK;

		// We need to wait for a message to appear
		ConditionVariableEntry entry;
		portRef->read_condition.Add(&entry);

		locker.Unlock();

		// block if no message, or, if B_TIMEOUT flag set, block with timeout
		status_t status = entry.Wait(flags, timeout);

		// re-lock
		BReference<Port> newPortRef = get_locked_port(id);
		if (newPortRef == NULL) {
			T(Read(id, 0, 0, 0, B_BAD_PORT_ID));
			return B_BAD_PORT_ID;
		}
		locker.SetTo(newPortRef->lock, true);

		if (newPortRef != portRef
			|| (is_port_closed(portRef) && portRef->messages.IsEmpty())) {
			// the port is no longer there
			T(Read(id, 0, 0, 0, B_BAD_PORT_ID));
			return B_BAD_PORT_ID;
		}

		if (status != B_OK) {
			T(Read(portRef, 0, status));
			return status;
		}
	}

	// determine tail & get the length of the message
	port_message* message = portRef->messages.Head();
	if (message == NULL) {
		panic("port %" B_PRId32 ": no messages found\n", portRef->id);
		return B_ERROR;
	}

	if (peekOnly) {
		size_t size = copy_port_message(message, _code, buffer, bufferSize,
			userCopy);

		T(Read(portRef, message->code, size));

		portRef->read_condition.NotifyOne();
			// we only peeked, but didn't grab the message
		return size;
	}

	portRef->messages.RemoveHead();
	portRef->total_count++;
	portRef->write_count++;
	portRef->read_count--;

	notify_port_select_events(portRef, B_EVENT_WRITE);
	portRef->write_condition.NotifyOne();
		// make one spot in queue available again for write

	T(Read(portRef, message->code, std::min(bufferSize, message->size)));

	locker.Unlock();

	size_t size = copy_port_message(message, _code, buffer, bufferSize,
		userCopy);

	put_port_message(message);
	return size;
}


/**
 * @brief Enqueues a single-vector message on a port.
 *
 * @param id          The port_id to write to.
 * @param msgCode     Integer message code to attach to the message.
 * @param buffer      Pointer to the message payload.
 * @param bufferSize  Size in bytes of @p buffer.
 * @retval B_OK           Message successfully enqueued.
 * @retval B_BAD_PORT_ID  The port does not exist or is closed.
 * @retval B_BAD_VALUE    @p bufferSize exceeds PORT_MAX_MESSAGE_SIZE.
 * @note  Delegates to writev_port_etc() with a single iovec and no timeout flags.
 */
status_t
write_port(port_id id, int32 msgCode, const void* buffer, size_t bufferSize)
{
	iovec vec = { (void*)buffer, bufferSize };

	return writev_port_etc(id, msgCode, &vec, 1, bufferSize, 0, 0);
}


/**
 * @brief Enqueues a message on a port with optional flags and timeout.
 *
 * @param id          The port_id to write to.
 * @param msgCode     Integer message code to attach to the message.
 * @param buffer      Pointer to the message payload.
 * @param bufferSize  Size in bytes of @p buffer.
 * @param flags       Wait flags (e.g., B_RELATIVE_TIMEOUT, B_ABSOLUTE_TIMEOUT,
 *                    B_CAN_INTERRUPT, PORT_FLAG_USE_USER_MEMCPY).
 * @param timeout     Timeout in microseconds; interpreted according to @p flags.
 * @retval B_OK           Message successfully enqueued.
 * @retval B_BAD_PORT_ID  The port does not exist or is closed.
 * @retval B_BAD_VALUE    @p bufferSize exceeds PORT_MAX_MESSAGE_SIZE.
 * @retval B_WOULD_BLOCK  Port is full and B_RELATIVE_TIMEOUT with timeout <= 0.
 * @retval B_TIMED_OUT    The wait for a free slot timed out.
 * @note  Delegates to writev_port_etc() with a single iovec.
 */
status_t
write_port_etc(port_id id, int32 msgCode, const void* buffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	iovec vec = { (void*)buffer, bufferSize };

	return writev_port_etc(id, msgCode, &vec, 1, bufferSize, flags, timeout);
}


/**
 * @brief Enqueues a scatter-gather message on a port with optional timeout.
 *
 * @param id          The port_id to write to.
 * @param msgCode     Integer message code for the message.
 * @param msgVecs     Array of iovec descriptors describing the payload segments.
 * @param vecCount    Number of entries in @p msgVecs.
 * @param bufferSize  Total payload size in bytes (sum of all iovec lengths to use);
 *                    must not exceed PORT_MAX_MESSAGE_SIZE.
 * @param flags       Wait flags (B_CAN_INTERRUPT, B_KILL_CAN_INTERRUPT,
 *                    B_RELATIVE_TIMEOUT, B_ABSOLUTE_TIMEOUT,
 *                    PORT_FLAG_USE_USER_MEMCPY).
 * @param timeout     Timeout in microseconds; relative timeouts are converted to
 *                    absolute internally to survive multiple wait steps.
 * @retval B_OK           Message successfully enqueued.
 * @retval B_BAD_PORT_ID  The port does not exist or is closed.
 * @retval B_BAD_VALUE    @p bufferSize exceeds PORT_MAX_MESSAGE_SIZE.
 * @retval B_WOULD_BLOCK  Port is full and B_RELATIVE_TIMEOUT with timeout <= 0.
 * @retval B_TIMED_OUT    The wait for a free slot timed out.
 * @note  Blocks if the port queue is full until a slot is freed or the timeout
 *        expires. Sender credentials (UID, GID, team) are recorded in the message.
 *        B_EVENT_READ is fired on registered select_info entries after enqueue.
 */
status_t
writev_port_etc(port_id id, int32 msgCode, const iovec* msgVecs,
	size_t vecCount, size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	if (!sPortsActive || id < 0)
		return B_BAD_PORT_ID;
	if (bufferSize > PORT_MAX_MESSAGE_SIZE)
		return B_BAD_VALUE;

	bool userCopy = (flags & PORT_FLAG_USE_USER_MEMCPY) != 0;

	// mask irrelevant flags (for acquire_sem() usage)
	flags &= B_CAN_INTERRUPT | B_KILL_CAN_INTERRUPT | B_RELATIVE_TIMEOUT
		| B_ABSOLUTE_TIMEOUT;
	if ((flags & B_RELATIVE_TIMEOUT) != 0
		&& timeout != B_INFINITE_TIMEOUT && timeout > 0) {
		// Make the timeout absolute, since we have more than one step where
		// we might have to wait
		flags = (flags & ~B_RELATIVE_TIMEOUT) | B_ABSOLUTE_TIMEOUT;
		timeout += system_time();
	}

	status_t status;
	port_message* message = NULL;

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL) {
		TRACE(("write_port_etc: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}
	MutexLocker locker(portRef->lock, true);

	if (is_port_closed(portRef)) {
		TRACE(("write_port_etc: port %ld closed\n", id));
		return B_BAD_PORT_ID;
	}

	if (portRef->write_count <= 0) {
		if ((flags & B_RELATIVE_TIMEOUT) != 0 && timeout <= 0)
			return B_WOULD_BLOCK;

		portRef->write_count--;

		// We need to block in order to wait for a free message slot
		ConditionVariableEntry entry;
		portRef->write_condition.Add(&entry);

		locker.Unlock();

		status = entry.Wait(flags, timeout);

		// re-lock
		BReference<Port> newPortRef = get_locked_port(id);
		if (newPortRef == NULL) {
			T(Write(id, 0, 0, 0, 0, B_BAD_PORT_ID));
			return B_BAD_PORT_ID;
		}
		locker.SetTo(newPortRef->lock, true);

		if (newPortRef != portRef || is_port_closed(portRef)) {
			// the port is no longer there
			T(Write(id, 0, 0, 0, 0, B_BAD_PORT_ID));
			return B_BAD_PORT_ID;
		}

		if (status != B_OK)
			goto error;
	} else
		portRef->write_count--;

	status = get_port_message(msgCode, bufferSize, flags, timeout,
		&message, *portRef);
	if (status != B_OK) {
		if (status == B_BAD_PORT_ID) {
			// the port had to be unlocked and is now no longer there
			T(Write(id, 0, 0, 0, 0, B_BAD_PORT_ID));
			return B_BAD_PORT_ID;
		}

		goto error;
	}

	// sender credentials
	message->sender = geteuid();
	message->sender_group = getegid();
	message->sender_team = team_get_current_team_id();

	if (bufferSize > 0) {
		size_t offset = 0;
		for (uint32 i = 0; i < vecCount; i++) {
			size_t bytes = msgVecs[i].iov_len;
			if (bytes > bufferSize)
				bytes = bufferSize;

			if (userCopy) {
				status_t status = user_memcpy(message->buffer + offset,
					msgVecs[i].iov_base, bytes);
				if (status != B_OK) {
					put_port_message(message);
					goto error;
				}
			} else
				memcpy(message->buffer + offset, msgVecs[i].iov_base, bytes);

			bufferSize -= bytes;
			if (bufferSize == 0)
				break;

			offset += bytes;
		}
	}

	portRef->messages.Add(message);
	portRef->read_count++;

	T(Write(id, portRef->read_count, portRef->write_count, message->code,
		message->size, B_OK));

	notify_port_select_events(portRef, B_EVENT_READ);
	portRef->read_condition.NotifyOne();
	return B_OK;

error:
	// Give up our slot in the queue again, and let someone else
	// try and fail
	T(Write(id, portRef->read_count, portRef->write_count, 0, 0, status));
	portRef->write_count++;
	notify_port_select_events(portRef, B_EVENT_WRITE);
	portRef->write_condition.NotifyOne();

	return status;
}


/**
 * @brief Transfers ownership of a port to a different team.
 *
 * @param id         The port_id of the port to reassign.
 * @param newTeamID  The team_id of the new owning team.
 * @retval B_OK           Ownership successfully transferred.
 * @retval B_BAD_PORT_ID  The port does not exist or has been deleted.
 * @retval B_BAD_TEAM_ID  The target team does not exist.
 * @note  Acquires both the old and new team's port-list locks in a consistent
 *        order (lower index first) to avoid deadlocks. The port's team_link is
 *        moved atomically under those locks. The port's own lock is also held
 *        for the duration of the state check.
 */
status_t
set_port_owner(port_id id, team_id newTeamID)
{
	TRACE(("set_port_owner(id = %ld, team = %ld)\n", id, newTeamID));

	if (id < 0)
		return B_BAD_PORT_ID;

	// get the new team
	Team* team = Team::Get(newTeamID);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	// get the port
	BReference<Port> portRef = get_locked_port(id);
	if (portRef == NULL) {
		TRACE(("set_port_owner: invalid port_id %ld\n", id));
		return B_BAD_PORT_ID;
	}
	MutexLocker locker(portRef->lock, true);

	// transfer ownership to other team
	if (team->id != portRef->owner) {
		uint8 firstLockIndex  = portRef->owner % kTeamListLockCount;
		uint8 secondLockIndex = team->id % kTeamListLockCount;

		// Avoid deadlocks: always lock lower index first
		if (secondLockIndex < firstLockIndex) {
			uint8 temp = secondLockIndex;
			secondLockIndex = firstLockIndex;
			firstLockIndex = temp;
		}

		MutexLocker oldTeamPortsListLocker(sTeamListLock[firstLockIndex]);
		MutexLocker newTeamPortsListLocker;
		if (firstLockIndex != secondLockIndex) {
			newTeamPortsListLocker.SetTo(sTeamListLock[secondLockIndex],
					false);
		}

		// Now that we have locked the team port lists, check the state again
		if (portRef->state == Port::kActive) {
			list_remove_link(&portRef->team_link);
			list_add_item(&team->port_list, portRef.Get());
			portRef->owner = team->id;
		} else {
			// Port was already deleted. We haven't changed anything yet so
			// we can cancel the operation.
			return B_BAD_PORT_ID;
		}
	}

	T(OwnerChange(portRef, team->id, B_OK));
	return B_OK;
}


//	#pragma mark - syscalls


/**
 * @brief Syscall wrapper: creates a port with a name supplied from user space.
 *
 * @param queueLength  Maximum number of messages the port may hold at once.
 * @param userName     User-space pointer to the NUL-terminated port name, or NULL.
 * @return             A valid port_id on success, or a negative error code.
 * @retval B_BAD_ADDRESS  @p userName is not a valid user-space address.
 * @note  Copies the name into a kernel buffer before calling create_port().
 */
port_id
_user_create_port(int32 queueLength, const char *userName)
{
	char name[B_OS_NAME_LENGTH];

	if (userName == NULL)
		return create_port(queueLength, NULL);

	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return create_port(queueLength, name);
}


/**
 * @brief Syscall wrapper: closes a port.
 *
 * @param id  The port_id of the port to close.
 * @return    B_OK on success, or a negative error code from close_port().
 */
status_t
_user_close_port(port_id id)
{
	return close_port(id);
}


/**
 * @brief Syscall wrapper: deletes a port.
 *
 * @param id  The port_id of the port to delete.
 * @return    B_OK on success, or a negative error code from delete_port().
 */
status_t
_user_delete_port(port_id id)
{
	return delete_port(id);
}


/**
 * @brief Syscall wrapper: finds a port by name supplied from user space.
 *
 * @param userName  User-space pointer to the NUL-terminated port name.
 * @return          The matching port_id on success, or a negative error code.
 * @retval B_BAD_VALUE    @p userName is NULL.
 * @retval B_BAD_ADDRESS  @p userName is not a valid user-space address.
 */
port_id
_user_find_port(const char *userName)
{
	char name[B_OS_NAME_LENGTH];

	if (userName == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userName)
		|| user_strlcpy(name, userName, B_OS_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	return find_port(name);
}


/**
 * @brief Syscall wrapper: retrieves port_info for the given port into user space.
 *
 * @param id        The port_id to query.
 * @param userInfo  User-space pointer to a port_info structure to populate.
 * @retval B_OK           Success; @p userInfo has been updated.
 * @retval B_BAD_VALUE    @p userInfo is NULL.
 * @retval B_BAD_ADDRESS  @p userInfo is not a valid user-space address.
 */
status_t
_user_get_port_info(port_id id, struct port_info *userInfo)
{
	struct port_info info;
	status_t status;

	if (userInfo == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = get_port_info(id, &info);

	// copy back to user space
	if (status == B_OK
		&& user_memcpy(userInfo, &info, sizeof(struct port_info)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall wrapper: iterates over a team's ports and returns one port_info
 *        per call into user space.
 *
 * @param team        The team_id whose ports are enumerated.
 * @param userCookie  User-space pointer to the int32 iteration cookie.
 * @param userInfo    User-space pointer to the port_info structure to populate.
 * @retval B_OK           Success; @p userInfo and @p userCookie updated.
 * @retval B_BAD_VALUE    Either pointer argument is NULL.
 * @retval B_BAD_ADDRESS  Either pointer is not a valid user-space address.
 */
status_t
_user_get_next_port_info(team_id team, int32 *userCookie,
	struct port_info *userInfo)
{
	struct port_info info;
	status_t status;
	int32 cookie;

	if (userCookie == NULL || userInfo == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userCookie) || !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	status = get_next_port_info(team, &cookie, &info);

	// copy back to user space
	if (user_memcpy(userCookie, &cookie, sizeof(int32)) < B_OK
		|| (status == B_OK && user_memcpy(userInfo, &info,
				sizeof(struct port_info)) < B_OK))
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall wrapper: returns the size of the next queued message with timeout.
 *
 * @param port     The port_id to query.
 * @param flags    Wait flags forwarded to port_buffer_size_etc().
 * @param timeout  Timeout in microseconds.
 * @return         Size in bytes of the next message's payload, or a negative error code.
 * @note  Handles syscall restart semantics via syscall_restart_handle_timeout_pre/post.
 */
ssize_t
_user_port_buffer_size_etc(port_id port, uint32 flags, bigtime_t timeout)
{
	syscall_restart_handle_timeout_pre(flags, timeout);

	status_t status = port_buffer_size_etc(port, flags | B_CAN_INTERRUPT,
		timeout);

	return syscall_restart_handle_timeout_post(status, timeout);
}


/**
 * @brief Syscall wrapper: returns the number of queued messages in a port.
 *
 * @param port  The port_id to query.
 * @return      Number of queued messages, or a negative error code.
 */
ssize_t
_user_port_count(port_id port)
{
	return port_count(port);
}


/**
 * @brief Syscall wrapper: transfers port ownership to another team.
 *
 * @param port  The port_id to transfer.
 * @param team  The target team_id.
 * @return      B_OK on success, or a negative error code from set_port_owner().
 */
status_t
_user_set_port_owner(port_id port, team_id team)
{
	return set_port_owner(port, team);
}


/**
 * @brief Syscall wrapper: dequeues one message from a port into user space.
 *
 * @param port        The port_id to read from.
 * @param userCode    User-space pointer to receive the message code; may be NULL.
 * @param userBuffer  User-space pointer to the destination buffer.
 * @param bufferSize  Size of @p userBuffer in bytes.
 * @param flags       Wait flags forwarded to read_port_etc().
 * @param timeout     Timeout in microseconds.
 * @return            Number of bytes copied on success, or a negative error code.
 * @retval B_BAD_VALUE    @p userBuffer is NULL but @p bufferSize != 0.
 * @retval B_BAD_ADDRESS  @p userCode or @p userBuffer is not a valid user address.
 * @note  Uses PORT_FLAG_USE_USER_MEMCPY so that payload is copied directly into
 *        user space. Handles syscall restart semantics.
 */
ssize_t
_user_read_port_etc(port_id port, int32 *userCode, void *userBuffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	int32 messageCode;
	ssize_t	bytesRead;

	syscall_restart_handle_timeout_pre(flags, timeout);

	if (userBuffer == NULL && bufferSize != 0)
		return B_BAD_VALUE;
	if ((userCode != NULL && !IS_USER_ADDRESS(userCode))
		|| (userBuffer != NULL && !IS_USER_ADDRESS(userBuffer)))
		return B_BAD_ADDRESS;

	bytesRead = read_port_etc(port, &messageCode, userBuffer, bufferSize,
		flags | PORT_FLAG_USE_USER_MEMCPY | B_CAN_INTERRUPT, timeout);

	if (bytesRead >= 0 && userCode != NULL
		&& user_memcpy(userCode, &messageCode, sizeof(int32)) < B_OK)
		return B_BAD_ADDRESS;

	return syscall_restart_handle_timeout_post(bytesRead, timeout);
}


/**
 * @brief Syscall wrapper: enqueues a single-buffer message from user space.
 *
 * @param port        The port_id to write to.
 * @param messageCode Integer message code.
 * @param userBuffer  User-space pointer to the message payload.
 * @param bufferSize  Size of @p userBuffer in bytes.
 * @param flags       Wait flags forwarded to writev_port_etc().
 * @param timeout     Timeout in microseconds.
 * @retval B_OK           Message enqueued successfully.
 * @retval B_BAD_VALUE    @p userBuffer is NULL but @p bufferSize != 0.
 * @retval B_BAD_ADDRESS  @p userBuffer is not a valid user-space address.
 * @note  Uses PORT_FLAG_USE_USER_MEMCPY; handles syscall restart semantics.
 */
status_t
_user_write_port_etc(port_id port, int32 messageCode, const void *userBuffer,
	size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	iovec vec = { (void *)userBuffer, bufferSize };

	syscall_restart_handle_timeout_pre(flags, timeout);

	if (userBuffer == NULL && bufferSize != 0)
		return B_BAD_VALUE;
	if (userBuffer != NULL && !IS_USER_ADDRESS(userBuffer))
		return B_BAD_ADDRESS;

	status_t status = writev_port_etc(port, messageCode, &vec, 1, bufferSize,
		flags | PORT_FLAG_USE_USER_MEMCPY | B_CAN_INTERRUPT, timeout);

	return syscall_restart_handle_timeout_post(status, timeout);
}


/**
 * @brief Syscall wrapper: enqueues a scatter-gather message from user space.
 *
 * @param port        The port_id to write to.
 * @param messageCode Integer message code.
 * @param userVecs    User-space pointer to an array of iovec descriptors.
 * @param vecCount    Number of entries in @p userVecs; must not exceed IOV_MAX.
 * @param bufferSize  Total payload size in bytes.
 * @param flags       Wait flags forwarded to writev_port_etc().
 * @param timeout     Timeout in microseconds.
 * @retval B_OK           Message enqueued successfully.
 * @retval B_BAD_VALUE    @p userVecs is NULL but @p bufferSize != 0, or
 *                        @p vecCount exceeds IOV_MAX.
 * @retval B_NO_MEMORY    Failed to allocate the kernel-side iovec copy buffer.
 * @note  Copies the iovec array from user space via get_iovecs_from_user() before
 *        forwarding to writev_port_etc(). Handles syscall restart semantics.
 */
status_t
_user_writev_port_etc(port_id port, int32 messageCode, const iovec *userVecs,
	size_t vecCount, size_t bufferSize, uint32 flags, bigtime_t timeout)
{
	syscall_restart_handle_timeout_pre(flags, timeout);

	if (userVecs == NULL && bufferSize != 0)
		return B_BAD_VALUE;
	if (vecCount > IOV_MAX)
		return B_BAD_VALUE;

	BStackOrHeapArray<iovec, 16> vecs(vecCount);
	if (!vecs.IsValid())
		return B_NO_MEMORY;

	if (userVecs != NULL && vecCount != 0) {
		status_t status = get_iovecs_from_user(userVecs, vecCount, vecs);
		if (status != B_OK)
			return status;
	}

	status_t status = writev_port_etc(port, messageCode, vecs, vecCount,
		bufferSize, flags | PORT_FLAG_USE_USER_MEMCPY | B_CAN_INTERRUPT,
		timeout);

	return syscall_restart_handle_timeout_post(status, timeout);
}


/**
 * @brief Syscall wrapper: retrieves extended message info into user space.
 *
 * @param port      The port_id to query.
 * @param userInfo  User-space pointer to a port_message_info structure.
 * @param infoSize  Must equal sizeof(port_message_info).
 * @param flags     Wait flags forwarded to _get_port_message_info_etc().
 * @param timeout   Timeout in microseconds.
 * @retval B_OK           Success; @p userInfo populated.
 * @retval B_BAD_VALUE    @p userInfo is NULL or @p infoSize is wrong.
 * @retval B_BAD_ADDRESS  @p userInfo is not a valid user-space address.
 * @note  Handles syscall restart semantics.
 */
status_t
_user_get_port_message_info_etc(port_id port, port_message_info *userInfo,
	size_t infoSize, uint32 flags, bigtime_t timeout)
{
	if (userInfo == NULL || infoSize != sizeof(port_message_info))
		return B_BAD_VALUE;

	syscall_restart_handle_timeout_pre(flags, timeout);

	port_message_info info;
	status_t error = _get_port_message_info_etc(port, &info, sizeof(info),
		flags | B_CAN_INTERRUPT, timeout);

	// copy info to userland
	if (error == B_OK && (!IS_USER_ADDRESS(userInfo)
			|| user_memcpy(userInfo, &info, sizeof(info)) != B_OK)) {
		error = B_BAD_ADDRESS;
	}

	return syscall_restart_handle_timeout_post(error, timeout);
}
