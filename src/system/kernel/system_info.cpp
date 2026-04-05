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
 *   Copyright 2004-2020, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini
 *       Axel Dörfler, axeld@pinc-software.de
 *       Paweł Dziepak, pdziepak@quarnos.org
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */

/**
 * @file system_info.cpp
 * @brief System information queries — CPU, memory, kernel version, and timing.
 *
 * Implements get_system_info(), get_cpu_info(), and related syscalls that
 * return hardware topology, memory usage statistics, kernel build version,
 * and real-time clock information to user space.
 *
 * @see cpu.cpp, vm/vm_page.cpp
 */


#include <ksystem_info.h>
#include <system_info.h>
#include <system_revision.h>
#include <arch/system_info.h>

#include <string.h>

#include <algorithm>

#include <OS.h>
#include <KernelExport.h>

#include <AutoDeleter.h>

#include <block_cache.h>
#include <cpu.h>
#include <debug.h>
#include <kernel.h>
#include <lock.h>
#include <Notifications.h>
#include <messaging.h>
#include <port.h>
#include <real_time_clock.h>
#include <sem.h>
#include <smp.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/vm_page.h>


const static int64 kKernelVersion = B_HAIKU_VERSION;
const static char *kKernelName = "kernel_" HAIKU_ARCH;


static int
dump_info(int argc, char **argv)
{
	kprintf("kernel build: %s %s (gcc%d %s), debug level %d\n", __DATE__,
		__TIME__, __GNUC__, __VERSION__, KDEBUG_LEVEL);
	kprintf("revision: %s\n\n", get_haiku_revision());

	kprintf("cpu count: %" B_PRId32 "\n", smp_get_num_cpus());

	for (int32 i = 0; i < smp_get_num_cpus(); i++)
		kprintf("  [%" B_PRId32 "] active time: %10" B_PRId64 ", interrupt"
			" time: %10" B_PRId64 ", irq time: %10" B_PRId64 "\n", i + 1,
			gCPU[i].active_time, gCPU[i].interrupt_time, gCPU[i].irq_time);

	// ToDo: Add page_faults
	kprintf("pages:\t\t%" B_PRIuPHYSADDR " (%" B_PRIuPHYSADDR " max)\n",
		vm_page_num_pages() - vm_page_num_free_pages(), vm_page_num_pages());

	kprintf("sems:\t\t%" B_PRId32 " (%" B_PRId32 " max)\n", sem_used_sems(),
		sem_max_sems());
	kprintf("ports:\t\t%" B_PRId32 " (%" B_PRId32 " max)\n", port_used_ports(),
			port_max_ports());
	kprintf("threads:\t%" B_PRId32 " (%" B_PRId32 " max)\n",
		thread_used_threads(), thread_max_threads());
	kprintf("teams:\t\t%" B_PRId32 " (%" B_PRId32 " max)\n", team_used_teams(),
		team_max_teams());

	return 0;
}


//	#pragma mark - user notifications


class SystemNotificationService : private NotificationListener {
public:
	SystemNotificationService()
	{
		mutex_init(&fLock, "system notification service");
	}

	status_t Init()
	{
		status_t error = fTeamListeners.Init();
		if (error != B_OK)
			return error;

		error = NotificationManager::Manager().AddListener("teams",
			TEAM_ADDED | TEAM_REMOVED | TEAM_EXEC, *this);
		if (error != B_OK)
			return error;

		error = NotificationManager::Manager().AddListener("threads",
			THREAD_ADDED | THREAD_REMOVED | TEAM_EXEC, *this);
		if (error != B_OK)
			return error;

		return B_OK;
	}

	status_t StartListening(int32 object, uint32 flags, port_id port,
		int32 token)
	{
		// check the parameters
		if ((object < 0 && object != -1) || port < 0)
			return B_BAD_VALUE;

		if ((flags & B_WATCH_SYSTEM_ALL) == 0
			|| (flags & ~(uint32)B_WATCH_SYSTEM_ALL) != 0) {
			return B_BAD_VALUE;
		}

		MutexLocker locker(fLock);

		// maybe the listener already exists
		ListenerList* listenerList;
		Listener* listener = _FindListener(object, port, token, listenerList);
		if (listener != NULL) {
			// just add the new flags
			listener->flags |= flags;
			return B_OK;
		}

		// create a new listener
		listener = new(std::nothrow) Listener;
		if (listener == NULL)
			return B_NO_MEMORY;
		ObjectDeleter<Listener> listenerDeleter(listener);

		listener->port = port;
		listener->token = token;
		listener->flags = flags;

		// if there's no list yet, create a new list
		if (listenerList == NULL) {
			listenerList = new(std::nothrow) ListenerList;
			if (listenerList == NULL)
				return B_NO_MEMORY;

			listenerList->object = object;

			fTeamListeners.Insert(listenerList);
		}

		listener->list = listenerList;
		listenerList->listeners.Add(listener);
		listenerDeleter.Detach();

		team_associate_data(listener);

		return B_OK;
	}

	status_t StopListening(int32 object, uint32 flags, port_id port,
		int32 token)
	{
		MutexLocker locker(fLock);

		// find the listener
		ListenerList* listenerList;
		Listener* listener = _FindListener(object, port, token, listenerList);
		if (listener == NULL)
			return B_ENTRY_NOT_FOUND;

		// clear the given flags
		listener->flags &= ~flags;

		if (listener->flags != 0)
			return B_OK;

		team_dissociate_data(listener);
		_RemoveListener(listener);

		return B_OK;
	}

private:
	struct ListenerList;

	struct Listener : AssociatedData {
		DoublyLinkedListLink<Listener>	listLink;
		ListenerList*					list;
		port_id							port;
		int32							token;
		uint32							flags;

		virtual void OwnerDeleted(AssociatedDataOwner* owner);
	};

	friend struct Listener;

	struct ListenerList {
		typedef DoublyLinkedList<Listener,
			DoublyLinkedListMemberGetLink<Listener, &Listener::listLink> > List;

		ListenerList*	hashNext;
		List			listeners;
		int32			object;
	};

	struct ListenerHashDefinition {
		typedef int32			KeyType;
		typedef	ListenerList	ValueType;

		size_t HashKey(int32 key) const
		{
			return key;
		}

		size_t Hash(const ListenerList* value) const
		{
			return HashKey(value->object);
		}

		bool Compare(int32 key, const ListenerList* value) const
		{
			return value->object == key;
		}

		ListenerList*& GetLink(ListenerList* value) const
		{
			return value->hashNext;
		}
	};

	typedef BOpenHashTable<ListenerHashDefinition> ListenerHash;

private:
	virtual void EventOccurred(NotificationService& service,
		const KMessage* event)
	{
		MutexLocker locker(fLock);

		int32 eventCode;
		int32 teamID = 0;
		if (event->FindInt32("event", &eventCode) != B_OK
			|| event->FindInt32("team", &teamID) != B_OK) {
			return;
		}

		int32 object;
		uint32 opcode;
		uint32 flags;

		// translate the event
		if (event->What() == TEAM_MONITOR) {
			switch (eventCode) {
				case TEAM_ADDED:
					opcode = B_TEAM_CREATED;
					flags = B_WATCH_SYSTEM_TEAM_CREATION;
					break;
				case TEAM_REMOVED:
					opcode = B_TEAM_DELETED;
					flags = B_WATCH_SYSTEM_TEAM_DELETION;
					break;
				case TEAM_EXEC:
					opcode = B_TEAM_EXEC;
					flags = B_WATCH_SYSTEM_TEAM_CREATION
						| B_WATCH_SYSTEM_TEAM_DELETION;
					break;
				default:
					return;
			}

			object = teamID;
		} else if (event->What() == THREAD_MONITOR) {
			if (event->FindInt32("thread", &object) != B_OK)
				return;

			switch (eventCode) {
				case THREAD_ADDED:
					opcode = B_THREAD_CREATED;
					flags = B_WATCH_SYSTEM_THREAD_CREATION;
					break;
				case THREAD_REMOVED:
					opcode = B_THREAD_DELETED;
					flags = B_WATCH_SYSTEM_THREAD_DELETION;
					break;
				case THREAD_NAME_CHANGED:
					opcode = B_THREAD_NAME_CHANGED;
					flags = B_WATCH_SYSTEM_THREAD_PROPERTIES;
					break;
				default:
					return;
			}
		} else
			return;

		// find matching listeners
		messaging_target targets[kMaxMessagingTargetCount];
		int32 targetCount = 0;

		_AddTargets(fTeamListeners.Lookup(teamID), flags, targets,
			targetCount, object, opcode);
		_AddTargets(fTeamListeners.Lookup(-1), flags, targets, targetCount,
			object, opcode);

		// send the message
		if (targetCount > 0)
			_SendMessage(targets, targetCount, object, opcode);
	}

	void _AddTargets(ListenerList* listenerList, uint32 flags,
		messaging_target* targets, int32& targetCount, int32 object,
		uint32 opcode)
	{
		if (listenerList == NULL)
			return;

		for (ListenerList::List::Iterator it
				= listenerList->listeners.GetIterator();
			Listener* listener = it.Next();) {
			if ((listener->flags & flags) == 0)
				continue;

			// array is full -- need to flush it first
			if (targetCount == kMaxMessagingTargetCount) {
				_SendMessage(targets, targetCount, object, opcode);
				targetCount = 0;
			}

			// add the listener
			targets[targetCount].port = listener->port;
			targets[targetCount++].token = listener->token;
		}
	}

	void _SendMessage(messaging_target* targets, int32 targetCount,
		int32 object, uint32 opcode)
	{
		// prepare the message
		char buffer[128];
		KMessage message;
		message.SetTo(buffer, sizeof(buffer), B_SYSTEM_OBJECT_UPDATE);
		message.AddInt32("opcode", opcode);
		if (opcode < B_THREAD_CREATED)
			message.AddInt32("team", object);
		else
			message.AddInt32("thread", object);

		// send it
		send_message(message.Buffer(), message.ContentSize(), targets,
			targetCount);
	}

	Listener* _FindListener(int32 object, port_id port, int32 token,
		ListenerList*& _listenerList)
	{
		_listenerList = fTeamListeners.Lookup(object);
		if (_listenerList == NULL)
			return NULL;

		for (ListenerList::List::Iterator it
				= _listenerList->listeners.GetIterator();
			Listener* listener = it.Next();) {
			if (listener->port == port && listener->token == token)
				return listener;
		}

		return NULL;
	}

	void _RemoveObsoleteListener(Listener* listener)
	{
		MutexLocker locker(fLock);
		_RemoveListener(listener);
	}

	void _RemoveListener(Listener* listener)
	{
		// no flags anymore -- remove the listener
		ListenerList* listenerList = listener->list;
		listenerList->listeners.Remove(listener);
		listener->ReleaseReference();

		if (listenerList->listeners.IsEmpty()) {
			// no listeners in the list anymore -- remove the list from the hash
			// table
			fTeamListeners.Remove(listenerList);
			delete listenerList;
		}
	}

private:
	static const int32 kMaxMessagingTargetCount = 8;

	mutex			fLock;
	ListenerHash	fTeamListeners;
};

static SystemNotificationService sSystemNotificationService;


void
SystemNotificationService::Listener::OwnerDeleted(AssociatedDataOwner* owner)
{
	sSystemNotificationService._RemoveObsoleteListener(this);
}


//	#pragma mark - private functions


static void
count_topology_nodes(const cpu_topology_node* node, uint32& count)
{
	count++;
	for (int32 i = 0; i < node->children_count; i++)
		count_topology_nodes(node->children[i], count);
}


static int32
get_logical_processor(const cpu_topology_node* node)
{
	while (node->level != CPU_TOPOLOGY_SMT) {
		ASSERT(node->children_count > 0);
		node = node->children[0];
	}

	return node->id;
}


static cpu_topology_node_info*
generate_topology_array(cpu_topology_node_info* topology,
	const cpu_topology_node* node, uint32& count)
{
	if (count == 0)
		return topology;

	static const topology_level_type mapTopologyLevels[] = { B_TOPOLOGY_SMT,
		B_TOPOLOGY_CORE, B_TOPOLOGY_PACKAGE, B_TOPOLOGY_ROOT };

	STATIC_ASSERT(sizeof(mapTopologyLevels) / sizeof(topology_level_type)
		== CPU_TOPOLOGY_LEVELS + 1);

	topology->id = node->id;
	topology->level = node->level;
	topology->type = mapTopologyLevels[node->level];

	arch_fill_topology_node(topology, get_logical_processor(node));

	count--;
	topology++;
	for (int32 i = 0; i < node->children_count && count > 0; i++)
		topology = generate_topology_array(topology, node->children[i], count);
	return topology;
}


//	#pragma mark -


/**
 * @brief Populate a system_info structure with current kernel statistics.
 *
 * Queries the real-time clock, VM page allocator, VM address space manager,
 * scheduler, and IPC subsystems to fill every field of @p info. The kernel
 * name and build timestamp are embedded at compile time.
 *
 * @param info Caller-supplied buffer that receives the system_info data.
 *             The buffer is zeroed before any field is written.
 * @retval B_OK Always succeeds under normal operating conditions.
 */
status_t
get_system_info(system_info* info)
{
	memset(info, 0, sizeof(system_info));

	info->boot_time = rtc_boot_time();
	info->cpu_count = smp_get_num_cpus();

	vm_page_get_stats(info);
	vm_get_info(info);

	info->used_threads = thread_used_threads();
	info->max_threads = thread_max_threads();
	info->used_teams = team_used_teams();
	info->max_teams = team_max_teams();
	info->used_ports = port_used_ports();
	info->max_ports = port_max_ports();
	info->used_sems = sem_used_sems();
	info->max_sems = sem_max_sems();

	info->kernel_version = kKernelVersion;
	strlcpy(info->kernel_name, kKernelName, B_FILE_NAME_LENGTH);
	strlcpy(info->kernel_build_date, __DATE__, B_OS_NAME_LENGTH);
	strlcpy(info->kernel_build_time, __TIME__, B_OS_NAME_LENGTH);
	info->abi = B_HAIKU_ABI;

	return B_OK;
}


typedef struct {
	bigtime_t	active_time;
	bool		enabled;
} beta2_cpu_info;


extern "C" status_t
__get_cpu_info(uint32 firstCPU, uint32 cpuCount, beta2_cpu_info* beta2_info)
{
	cpu_info info[cpuCount];
	status_t err = _get_cpu_info_etc(firstCPU, cpuCount, info, sizeof(cpu_info));
	if (err == B_OK) {
		for (uint32 i = 0; i < cpuCount; i++) {
			beta2_info[i].active_time = info[i].active_time;
			beta2_info[i].enabled = info[i].enabled;
		}
	}
	return err;
}


/**
 * @brief Retrieve per-CPU activity information for a range of processors.
 *
 * Fills an array of cpu_info structures with the active time, enabled state,
 * and current frequency for CPUs in the range [@p firstCPU,
 * @p firstCPU + @p cpuCount). Copies the data to @p info in small batches to
 * minimise stack pressure and avoid large temporary allocations.
 *
 * @param firstCPU  Index of the first CPU to query (0-based).
 * @param cpuCount  Number of CPUs to query.
 * @param info      Caller-supplied array of cpu_info that receives the data.
 *                  Must be large enough for @p cpuCount entries.
 * @param size      Must equal sizeof(cpu_info); used to detect ABI mismatches.
 * @retval B_OK          Data written successfully for all requested CPUs.
 * @retval B_BAD_VALUE   @p cpuCount is zero, @p size is wrong, or @p firstCPU
 *                       is out of range.
 * @retval B_BAD_ADDRESS @p info is not a valid user-space address.
 */
status_t
_get_cpu_info_etc(uint32 firstCPU, uint32 cpuCount, cpu_info* info, size_t size)
{
	if (cpuCount == 0)
		return B_OK;
	if (size != sizeof(cpu_info))
		return B_BAD_VALUE;
	if (firstCPU >= (uint32)smp_get_num_cpus())
		return B_BAD_VALUE;

	const uint32 endCPU = firstCPU + std::min(cpuCount, smp_get_num_cpus() - firstCPU);

	// This function is called very often from userland by applications
	// that display CPU usage information, so we want to keep this as
	// optimized and touch as little as possible. Hence, we avoid use
	// of an allocated temporary buffer.

	cpu_info localInfo[8];
	for (uint32 cpuIdx = firstCPU; cpuIdx < endCPU; ) {
		uint32 localIdx;
		for (localIdx = 0; cpuIdx < endCPU && localIdx < B_COUNT_OF(localInfo);
				cpuIdx++, localIdx++) {
			localInfo[localIdx].active_time = cpu_get_active_time(cpuIdx);
			localInfo[localIdx].enabled = !gCPU[cpuIdx].disabled;
			localInfo[localIdx].current_frequency = cpu_frequency(cpuIdx);
		}

		if (user_memcpy(info, localInfo, sizeof(cpu_info) * localIdx) != B_OK)
			return B_BAD_ADDRESS;
		info += localIdx;
	}

	return B_OK;
}


/**
 * @brief Initialise the system information subsystem.
 *
 * Registers the "info" kernel debugger command and delegates to the
 * architecture-specific initialisation routine.
 *
 * @param args Kernel boot arguments passed through to arch_system_info_init().
 * @return B_OK on success, or an error code from arch_system_info_init().
 */
status_t
system_info_init(struct kernel_args *args)
{
	add_debugger_command("info", &dump_info, "System info");

	return arch_system_info_init(args);
}


/**
 * @brief Initialise the system notification service.
 *
 * Constructs the SystemNotificationService singleton and subscribes it to
 * team and thread notification events so that user-space watchers receive
 * B_SYSTEM_OBJECT_UPDATE messages when teams or threads are created or destroyed.
 *
 * @retval B_OK The notification service was started successfully.
 * @return Any error code returned by SystemNotificationService::Init().
 */
status_t
system_notifications_init()
{
	new (&sSystemNotificationService) SystemNotificationService;

	status_t error = sSystemNotificationService.Init();
	if (error != B_OK) {
		panic("system_info_init(): Failed to init system notification service");
		return error;
	}

	return B_OK;
}


//	#pragma mark -


/**
 * @brief Syscall: copy system_info to user space.
 *
 * Calls get_system_info() to gather current statistics and copies the result
 * to the user-space buffer @p userInfo.
 *
 * @param userInfo User-space pointer to a system_info buffer.
 * @retval B_OK          Information copied successfully.
 * @retval B_BAD_ADDRESS @p userInfo is @c NULL or not a valid user-space pointer.
 */
status_t
_user_get_system_info(system_info* userInfo)
{
	if (userInfo == NULL || !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	system_info info;
	status_t status = get_system_info(&info);
	if (status == B_OK) {
		if (user_memcpy(userInfo, &info, sizeof(system_info)) < B_OK)
			return B_BAD_ADDRESS;

		return B_OK;
	}

	return status;
}


/**
 * @brief Syscall: copy per-CPU info for a range of CPUs to user space.
 *
 * Delegates directly to _get_cpu_info_etc(), which performs the user_memcpy
 * internally, so @p userInfo must be a valid user-space address.
 *
 * @param firstCPU  Index of the first CPU to query (0-based).
 * @param cpuCount  Number of CPUs to query.
 * @param userInfo  User-space array of cpu_info that receives the data.
 * @retval B_OK          Data written successfully.
 * @retval B_BAD_ADDRESS @p userInfo is @c NULL or not a valid user-space pointer.
 * @retval B_BAD_VALUE   @p firstCPU is out of range or @p cpuCount is zero.
 */
status_t
_user_get_cpu_info(uint32 firstCPU, uint32 cpuCount, cpu_info* userInfo)
{
	if (userInfo == NULL || !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	return _get_cpu_info_etc(firstCPU, cpuCount, userInfo, sizeof(cpu_info));
}


/**
 * @brief Syscall: retrieve the CPU topology tree as a flat array.
 *
 * If @p topologyInfos is @c NULL, writes only the total node count to
 * @p topologyInfoCount and returns. Otherwise, fills up to @p *topologyInfoCount
 * entries with topology data and updates @p topologyInfoCount with the number
 * of nodes actually written.
 *
 * @param topologyInfos      User-space array that receives cpu_topology_node_info
 *                           entries, or @c NULL to query the required count.
 * @param topologyInfoCount  In/out: capacity on entry, actual count on exit.
 * @retval B_OK          Data (or count) written successfully.
 * @retval B_BAD_ADDRESS A pointer argument is not a valid user-space address.
 * @retval B_NO_MEMORY   Kernel-side temporary buffer allocation failed.
 */
status_t
_user_get_cpu_topology_info(cpu_topology_node_info* topologyInfos,
	uint32* topologyInfoCount)
{
	if (topologyInfoCount == NULL || !IS_USER_ADDRESS(topologyInfoCount))
		return B_BAD_ADDRESS;

	const cpu_topology_node* node = get_cpu_topology();

	uint32 count = 0;
	count_topology_nodes(node, count);

	if (topologyInfos == NULL)
		return user_memcpy(topologyInfoCount, &count, sizeof(uint32));
	else if (!IS_USER_ADDRESS(topologyInfos))
		return B_BAD_ADDRESS;

	uint32 userCount;
	status_t error = user_memcpy(&userCount, topologyInfoCount, sizeof(uint32));
	if (error != B_OK)
		return error;
	if (userCount == 0)
		return B_OK;
	count = std::min(count, userCount);

	cpu_topology_node_info* topology
		= new(std::nothrow) cpu_topology_node_info[count];
	if (topology == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<cpu_topology_node_info> _(topology);
	memset(topology, 0, sizeof(cpu_topology_node_info) * count);

	uint32 nodesLeft = count;
	generate_topology_array(topology, node, nodesLeft);
	ASSERT(nodesLeft == 0);

	error = user_memcpy(topologyInfos, topology,
		sizeof(cpu_topology_node_info) * count);
	if (error != B_OK)
		return error;
	return user_memcpy(topologyInfoCount, &count, sizeof(uint32));
}


/**
 * @brief Syscall: register a user-space watcher for system object events.
 *
 * Asks the SystemNotificationService to send B_SYSTEM_OBJECT_UPDATE messages
 * to @p port/@p token whenever events matching @p flags occur for @p object
 * (a team ID, thread ID, or -1 for all).
 *
 * @param object  Team or thread ID to watch, or -1 to watch all objects.
 * @param flags   B_WATCH_SYSTEM_* flags specifying which events to monitor.
 * @param port    Destination port for notification messages.
 * @param token   BHandler token embedded in each notification message.
 * @retval B_OK        Listener registered (or flags updated) successfully.
 * @retval B_BAD_VALUE Invalid @p object, @p port, or @p flags combination.
 * @retval B_NO_MEMORY Allocation of a new listener record failed.
 */
status_t
_user_start_watching_system(int32 object, uint32 flags, port_id port,
	int32 token)
{
	return sSystemNotificationService.StartListening(object, flags, port,
		token);
}


/**
 * @brief Syscall: unregister a user-space watcher for system object events.
 *
 * Clears the specified @p flags for the listener identified by
 * (@p object, @p port, @p token). If no flags remain the listener is removed
 * entirely.
 *
 * @param object  Team or thread ID that was being watched, or -1.
 * @param flags   B_WATCH_SYSTEM_* flags to stop monitoring.
 * @param port    Destination port of the listener to remove.
 * @param token   BHandler token of the listener to remove.
 * @retval B_OK              Flags cleared (or listener removed) successfully.
 * @retval B_ENTRY_NOT_FOUND No matching listener was found.
 */
status_t
_user_stop_watching_system(int32 object, uint32 flags, port_id port,
	int32 token)
{
	return sSystemNotificationService.StopListening(object, flags, port, token);
}
