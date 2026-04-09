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
 *   Copyright 2002-2024, Haiku. All rights reserved.
 *   This file may be used under the terms of the MIT License.
 *
 *   Authors:
 *      Dario Casalinuovo
 *      Marcus Overhagen
 */

/** @file TimeSource.cpp
 *  @brief Implements BTimeSource: the base class for all media clock providers. */


#include <TimeSource.h>

#include <Autolock.h>

#include <string.h>

#include "MediaDebug.h"
#include "DataExchange.h"
#include "ServerInterface.h"
#include "TimeSourceObject.h"
#include "TMap.h"

#define DEBUG_TIMESOURCE 0

#if DEBUG_TIMESOURCE
	#define TRACE_TIMESOURCE printf
#else
	#define TRACE_TIMESOURCE if (1) {} else printf
#endif

namespace BPrivate { namespace media {

#define _atomic_read(p) 	atomic_or((p), 0)

// must be multiple of page size
#define TS_AREA_SIZE		B_PAGE_SIZE
// must be power of two
#define TS_INDEX_COUNT		128

// sizeof(TimeSourceTransmit) must be <= TS_AREA_SIZE
struct TimeSourceTransmit
{
	int32 readindex;
	int32 writeindex;
	int32 isrunning;
	bigtime_t realtime[TS_INDEX_COUNT];
	bigtime_t perftime[TS_INDEX_COUNT];
	float drift[TS_INDEX_COUNT];
};

#define MAX_SLAVE_NODES 300


/** @brief Maintains the set of slave nodes that are driven by a BTimeSource. */
class SlaveNodes : public BLocker
{
public:
								SlaveNodes();
								~SlaveNodes();

	int32						CountSlaves() const;
	bool						GetNextSlave(port_id** id);
	void						Rewind();

	bool						InsertSlave(const media_node& node);
	bool						RemoveSlave(const media_node& node);
private:
	Map<media_node_id, port_id>	fSlaveList;
};


/** @brief Constructs the slave-node list locker. */
SlaveNodes::SlaveNodes()
	:
	BLocker("BTimeSource slavenodes")
{
}


/** @brief Destroys the slave-node list. */
SlaveNodes::~SlaveNodes()
{
	fSlaveList.MakeEmpty();
}


/** @brief Returns the number of slave nodes currently registered.
 *  @return The slave count. */
int32
SlaveNodes::CountSlaves() const
{
	return fSlaveList.CountItems();
}


/** @brief Advances the internal iterator to the next slave and stores a pointer to
 *         its port_id.
 *  @param id  Receives a pointer to the next slave's port_id.
 *  @return \c true if a next slave exists, \c false if the list is exhausted. */
bool
SlaveNodes::GetNextSlave(port_id** id)
{
	return fSlaveList.GetNext(id);
}


/** @brief Resets the internal iterator to the beginning of the slave list. */
void
SlaveNodes::Rewind()
{
	fSlaveList.Rewind();
}


/** @brief Inserts a slave node into the list.
 *  @param node  The media_node to add as a slave.
 *  @return \c true on success, \c false on failure. */
bool
SlaveNodes::InsertSlave(const media_node& node)
{
	return fSlaveList.Insert(node.node, node.port);
}


/** @brief Removes a slave node from the list.
 *  @param node  The media_node to remove.
 *  @return \c true on success, \c false if the node was not found. */
bool
SlaveNodes::RemoveSlave(const media_node& node)
{
	return fSlaveList.Remove(node.node);
}


} } // namespace BPrivate::media


/*************************************************************
 * protected BTimeSource
 *************************************************************/

/** @brief Destructor; deletes the shared-memory area and the slave-node list. */
BTimeSource::~BTimeSource()
{
	CALLED();
	if (fArea > 0)
		delete_area(fArea);
	delete fSlaveNodes;
}

/*************************************************************
 * public BTimeSource
 *************************************************************/

/** @brief Sleeps until the given performance time (adjusted for latency), optionally
 *         retrying on signal interrupts.
 *  @param performance_time  Target performance time to snooze until.
 *  @param with_latency      Latency adjustment subtracted from the real wake-up time.
 *  @param retry_signals     If \c true, automatically retries on B_INTERRUPTED.
 *  @return B_OK on success, or a snooze error code on failure. */
status_t
BTimeSource::SnoozeUntil(bigtime_t performance_time,
	bigtime_t with_latency, bool retry_signals)
{
	CALLED();
	bigtime_t time;
	status_t err;
	do {
		time = RealTimeFor(performance_time, with_latency);
		err = snooze_until(time, B_SYSTEM_TIMEBASE);
	} while (err == B_INTERRUPTED && retry_signals);
	return err;
}


/** @brief Returns the current performance time as seen by this time source.
 *  @return The current performance time in microseconds. */
bigtime_t
BTimeSource::Now()
{
	PRINT(8, "CALLED BTimeSource::Now()\n");
	return PerformanceTimeFor(RealTime());
}


/** @brief Converts a real time value to the corresponding performance time.
 *  @param real_time  System real time in microseconds.
 *  @return The performance time corresponding to \a real_time. */
bigtime_t
BTimeSource::PerformanceTimeFor(bigtime_t real_time)
{
	PRINT(8, "CALLED BTimeSource::PerformanceTimeFor()\n");
	bigtime_t last_perf_time;
	bigtime_t last_real_time;
	float last_drift;

	if (GetTime(&last_perf_time, &last_real_time, &last_drift) != B_OK)
		debugger("BTimeSource::PerformanceTimeFor: GetTime failed");

	bigtime_t real_time_difference = real_time - last_real_time;
	if (real_time_difference >= (1 << FLT_MANT_DIG)) {
		// The difference is too large fit in a float.
		if (last_drift == 1.0f)
			return last_perf_time + real_time_difference;

		debugger("BTimeSource::PerformanceTimeFor: real time too large");
	}

	return last_perf_time + (bigtime_t)(real_time_difference * last_drift);
}


/** @brief Converts a performance time to the real time at which it should be
 *         processed, accounting for latency.
 *  @param performance_time  Target performance time in microseconds.
 *  @param with_latency      Latency (in microseconds) to subtract from the result.
 *  @return The real time corresponding to \a performance_time minus \a with_latency. */
bigtime_t
BTimeSource::RealTimeFor(bigtime_t performance_time,
	bigtime_t with_latency)
{
	PRINT(8, "CALLED BTimeSource::RealTimeFor()\n");

	if (fIsRealtime)
		return performance_time - with_latency;

	bigtime_t last_perf_time;
	bigtime_t last_real_time;
	float last_drift;

	if (GetTime(&last_perf_time, &last_real_time, &last_drift) != B_OK)
		debugger("BTimeSource::RealTimeFor: GetTime failed");

	bigtime_t perf_time_difference = performance_time - last_perf_time;
	if (perf_time_difference >= (1 << FLT_MANT_DIG)) {
		// The difference is too large to fit in a float.
		if (last_drift == 1.0f)
			return last_real_time - with_latency + perf_time_difference;

		debugger("BTimeSource::RealTimeFor: performance time too large");
	}

	return last_real_time - with_latency
		+ (bigtime_t)(perf_time_difference / last_drift);
}


/** @brief Returns whether the time source is currently running.
 *  @return \c true if the time source is running, \c false otherwise. */
bool
BTimeSource::IsRunning()
{
	PRINT(8, "CALLED BTimeSource::IsRunning()\n");

	bool isrunning;

	// The system time source is always running
	if (fIsRealtime)
		isrunning = true;
	else
		isrunning = fBuf ? atomic_add(&fBuf->isrunning, 0) : fStarted;

	TRACE_TIMESOURCE("BTimeSource::IsRunning() node %" B_PRId32 ", port %"
		B_PRId32 ", %s\n", fNodeID, fControlPort, isrunning ? "yes" : "no");
	return isrunning;
}


/** @brief Retrieves the most recently published performance/real-time/drift triplet.
 *  @param performance_time  Receives the last published performance time.
 *  @param real_time         Receives the last published real time.
 *  @param drift             Receives the last published drift factor.
 *  @return B_OK on success. */
status_t
BTimeSource::GetTime(bigtime_t* performance_time,
	bigtime_t* real_time, float* drift)
{
	PRINT(8, "CALLED BTimeSource::GetTime()\n");

	if (fIsRealtime) {
		*performance_time = *real_time = system_time();
		*drift = 1.0f;
		return B_OK;
	}

	if (fBuf == NULL)
		debugger("BTimeSource::GetTime: fBuf == NULL");

	int32 index = _atomic_read(&fBuf->readindex);
	index &= (TS_INDEX_COUNT - 1);
	*real_time = fBuf->realtime[index];
	*performance_time = fBuf->perftime[index];
	*drift = fBuf->drift[index];

	TRACE_TIMESOURCE("BTimeSource::GetTime     timesource %" B_PRId32
		", perf %16" B_PRId64 ", real %16" B_PRId64 ", drift %2.2f\n", ID(),
		*performance_time, *real_time, *drift);
	return B_OK;
}


/** @brief Returns the current system real time in microseconds.
 *  @return The value of system_time(). */
bigtime_t
BTimeSource::RealTime()
{
	PRINT(8, "CALLED BTimeSource::RealTime()\n");
	return system_time();
}


/** @brief Returns the latency from when a start request is issued to when the
 *         time source actually begins running.
 *  @param out_latency  Receives the start latency in microseconds.
 *  @return B_OK always (base implementation returns 0 latency). */
status_t
BTimeSource::GetStartLatency(bigtime_t* out_latency)
{
	CALLED();
	*out_latency = 0;
	return B_OK;
}

/*************************************************************
 * protected BTimeSource
 *************************************************************/


/** @brief Constructor for real time-source subclasses; registers the node as a
 *         B_TIME_SOURCE.  The shared-memory area is created later in FinishCreate(). */
BTimeSource::BTimeSource()
	:
	BMediaNode("This one is never called"),
	fStarted(false),
	fArea(-1),
	fBuf(NULL),
	fSlaveNodes(new BPrivate::media::SlaveNodes),
	fIsRealtime(false)
{
	CALLED();
	AddNodeKind(B_TIME_SOURCE);
	// This constructor is only called by real time sources that inherit
	// BTimeSource. We create the communication area in FinishCreate(),
	// since we don't have a correct ID() until this node is registered.
}


/** @brief Dispatches incoming time-source port messages to the appropriate handler.
 *  @param message  The port message code.
 *  @param rawdata  Pointer to the raw message payload.
 *  @param size     Size of the payload in bytes.
 *  @return B_OK if the message was handled, B_ERROR otherwise. */
status_t
BTimeSource::HandleMessage(int32 message, const void* rawdata,
	size_t size)
{
	PRINT(4, "BTimeSource::HandleMessage %#" B_PRIx32 ", node %" B_PRId32 "\n",
		message, fNodeID);
	status_t rv;
	switch (message) {
		case TIMESOURCE_OP:
		{
			const time_source_op_info* data
				= static_cast<const time_source_op_info*>(rawdata);

			status_t result;
			result = TimeSourceOp(*data, NULL);
			if (result != B_OK) {
				ERROR("BTimeSource::HandleMessage: TimeSourceOp failed\n");
			}

			switch (data->op) {
				case B_TIMESOURCE_START:
					DirectStart(data->real_time);
					break;
				case B_TIMESOURCE_STOP:
					DirectStop(data->real_time, false);
					break;
				case B_TIMESOURCE_STOP_IMMEDIATELY:
					DirectStop(data->real_time, true);
					break;
				case B_TIMESOURCE_SEEK:
					DirectSeek(data->performance_time, data->real_time);
					break;
			}
			return B_OK;
		}

		case TIMESOURCE_ADD_SLAVE_NODE:
		{
			const timesource_add_slave_node_command* data
				= static_cast<const timesource_add_slave_node_command*>(rawdata);
			DirectAddMe(data->node);
			return B_OK;
		}

		case TIMESOURCE_REMOVE_SLAVE_NODE:
		{
			const timesource_remove_slave_node_command* data
				= static_cast<const timesource_remove_slave_node_command*>(rawdata);
			DirectRemoveMe(data->node);
			return B_OK;
		}

		case TIMESOURCE_GET_START_LATENCY:
		{
			const timesource_get_start_latency_request* request
				= static_cast<const timesource_get_start_latency_request*>(rawdata);
			timesource_get_start_latency_reply reply;
			rv = GetStartLatency(&reply.start_latency);
			request->SendReply(rv, &reply, sizeof(reply));
			return B_OK;
		}
	}
	return B_ERROR;
}


/** @brief Publishes a new performance/real-time/drift triplet to the shared-memory
 *         buffer so that slave nodes can read it without IPC overhead.
 *  @param performance_time  Current performance time in microseconds.
 *  @param real_time         Corresponding real (system) time in microseconds.
 *  @param drift             The current drift factor (must be > 0). */
void
BTimeSource::PublishTime(bigtime_t performance_time,
	bigtime_t real_time, float drift)
{
	TRACE_TIMESOURCE("BTimeSource::PublishTime timesource %" B_PRId32
		", perf %16" B_PRId64 ", real %16" B_PRId64 ", drift %2.2f\n", ID(),
		performance_time, real_time, drift);
	if (fBuf == NULL) {
		ERROR("BTimeSource::PublishTime timesource %" B_PRId32
			", fBuf = NULL\n", ID());
		fStarted = true;
		return;
	}

	if (drift <= 0.0f)
		debugger("BTimeSource::PublishTime: drift must be > 0");

	int32 index = atomic_add(&fBuf->writeindex, 1);
	index &= (TS_INDEX_COUNT - 1);
	fBuf->realtime[index] = real_time;
	fBuf->perftime[index] = performance_time;
	fBuf->drift[index] = drift;
	atomic_add(&fBuf->readindex, 1);
}


/** @brief Sends a NODE_TIME_WARP command to all currently slaved nodes.
 *  @param at_real_time          The real time at which the warp takes effect.
 *  @param new_performance_time  The new performance time that corresponds to
 *                               \a at_real_time. */
void
BTimeSource::BroadcastTimeWarp(bigtime_t at_real_time,
	bigtime_t new_performance_time)
{
	CALLED();
	ASSERT(fSlaveNodes != NULL);

	// calls BMediaNode::TimeWarp() of all slaved nodes

	TRACE("BTimeSource::BroadcastTimeWarp: at_real_time %" B_PRId64
		", new_performance_time %" B_PRId64 "\n", at_real_time,
		new_performance_time);

	BAutolock lock(fSlaveNodes);

	port_id* port = NULL;
	while (fSlaveNodes->GetNextSlave(&port) == true) {
		node_time_warp_command cmd;
		cmd.at_real_time = at_real_time;
		cmd.to_performance_time = new_performance_time;
		SendToPort(*port, NODE_TIME_WARP,
			&cmd, sizeof(cmd));
	}
	fSlaveNodes->Rewind();
}


/** @brief Sends a NODE_SET_RUN_MODE command to all currently slaved nodes.
 *  @param mode  The new run mode to propagate. */
void
BTimeSource::SendRunMode(run_mode mode)
{
	CALLED();
	ASSERT(fSlaveNodes != NULL);

	// send the run mode change to all slaved nodes

	BAutolock lock(fSlaveNodes);

	port_id* port = NULL;
	while (fSlaveNodes->GetNextSlave(&port) == true) {
		node_set_run_mode_command cmd;
		cmd.mode = mode;
		SendToPort(*port, NODE_SET_RUN_MODE,
			&cmd, sizeof(cmd));
	}
	fSlaveNodes->Rewind();
}


/** @brief Overrides BMediaNode::SetRunMode() to also propagate the new mode to all
 *         slave nodes via SendRunMode().
 *  @param mode  The new run mode. */
void
BTimeSource::SetRunMode(run_mode mode)
{
	CALLED();
	BMediaNode::SetRunMode(mode);
	SendRunMode(mode);
}
/*************************************************************
 * private BTimeSource
 *************************************************************/

/*
//unimplemented
BTimeSource::BTimeSource(const BTimeSource &clone)
BTimeSource &BTimeSource::operator=(const BTimeSource &clone)
*/

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTimeSource::_Reserved_TimeSource_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTimeSource::_Reserved_TimeSource_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTimeSource::_Reserved_TimeSource_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTimeSource::_Reserved_TimeSource_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTimeSource::_Reserved_TimeSource_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTimeSource::_Reserved_TimeSource_5(void *) { return B_ERROR; }

/** @brief Constructor used by TimeSourceObject shadow proxies; clones the shared-memory
 *         area created by the real time source.
 *  @param id  The media_node_id of the real time source being proxied. */
/* explicit */
BTimeSource::BTimeSource(media_node_id id)
	:
	BMediaNode("This one is never called"),
	fStarted(false),
	fArea(-1),
	fBuf(NULL),
	fSlaveNodes(NULL),
	fIsRealtime(false)
{
	CALLED();
	AddNodeKind(B_TIME_SOURCE);
	ASSERT(id > 0);

	// This constructor is only called by the derived
	// BPrivate::media::TimeSourceObject objects
	// We create a clone of the communication area.
	char name[32];
	sprintf(name, "__timesource_buf_%" B_PRId32, id);
	area_id area = find_area(name);
	if (area <= 0) {
		ERROR("BTimeSource::BTimeSource couldn't find area, node %" B_PRId32
			"\n", id);
		return;
	}
	sprintf(name, "__cloned_timesource_buf_%" B_PRId32, id);

	void** buf = reinterpret_cast<void**>
		(const_cast<BPrivate::media::TimeSourceTransmit**>(&fBuf));

	fArea = clone_area(name, buf, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, area);

	if (fArea <= 0) {
		ERROR("BTimeSource::BTimeSource couldn't clone area, node %" B_PRId32
			"\n", id);
		return;
	}
}


/** @brief Creates the shared-memory area used to publish time data to slave nodes.
 *         Must be called after the node has been registered and has a valid ID(). */
void
BTimeSource::FinishCreate()
{
	CALLED();

	char name[32];
	sprintf(name, "__timesource_buf_%" B_PRId32, ID());

	void** buf = reinterpret_cast<void**>
		(const_cast<BPrivate::media::TimeSourceTransmit**>(&fBuf));

	fArea = create_area(name, buf, B_ANY_ADDRESS, TS_AREA_SIZE,
		B_FULL_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (fArea <= 0) {
		ERROR("BTimeSource::BTimeSource couldn't create area, node %" B_PRId32
			"\n", ID());
		fBuf = NULL;
		return;
	}
	fBuf->readindex = 0;
	fBuf->writeindex = 1;
	fBuf->realtime[0] = 0;
	fBuf->perftime[0] = 0;
	fBuf->drift[0] = 1.0f;
	fBuf->isrunning = fStarted;
}


/** @brief Removes a node from the set of slaves driven by this time source.
 *
 *  For shadow time sources the removal is forwarded via the control port;
 *  for real time sources it is performed directly.
 *
 *  @param node  The BMediaNode to remove from the slave list.
 *  @return B_OK always. */
status_t
BTimeSource::RemoveMe(BMediaNode* node)
{
	CALLED();
	if (fKinds & NODE_KIND_SHADOW_TIMESOURCE) {
		timesource_remove_slave_node_command cmd;
		cmd.node = node->Node();
		SendToPort(fControlPort, TIMESOURCE_REMOVE_SLAVE_NODE,
			&cmd, sizeof(cmd));
	} else {
		DirectRemoveMe(node->Node());
	}
	return B_OK;
}


/** @brief Adds a node to the set of slaves driven by this time source.
 *
 *  For shadow time sources the addition is forwarded via the control port;
 *  for real time sources it is performed directly.
 *
 *  @param node  The BMediaNode to add to the slave list.
 *  @return B_OK always. */
status_t
BTimeSource::AddMe(BMediaNode* node)
{
	CALLED();
	if (fKinds & NODE_KIND_SHADOW_TIMESOURCE) {
		timesource_add_slave_node_command cmd;
		cmd.node = node->Node();
		SendToPort(fControlPort, TIMESOURCE_ADD_SLAVE_NODE, &cmd, sizeof(cmd));
	} else {
		DirectAddMe(node->Node());
	}
	return B_OK;
}


/** @brief Directly inserts a node into the slave list and starts the time source
 *         when the first slave is added.
 *  @param node  The media_node to add as a slave. */
void
BTimeSource::DirectAddMe(const media_node& node)
{
	CALLED();
	ASSERT(fSlaveNodes != NULL);
	BAutolock lock(fSlaveNodes);

	if (fSlaveNodes->CountSlaves() == MAX_SLAVE_NODES) {
		ERROR("BTimeSource::DirectAddMe reached maximum number of slaves\n");
		return;
	}
	if (fNodeID == node.node) {
		ERROR("BTimeSource::DirectAddMe should not add itself to slave nodes\n");
		return;
	}

	if (fSlaveNodes->InsertSlave(node) != true) {
		ERROR("BTimeSource::DirectAddMe failed\n");
		return;
	}

	if (fSlaveNodes->CountSlaves() == 1) {
		// start the time source
		time_source_op_info msg;
		msg.op = B_TIMESOURCE_START;
		msg.real_time = RealTime();

		TRACE_TIMESOURCE("starting time source %" B_PRId32 "\n", ID());

		write_port(fControlPort, TIMESOURCE_OP, &msg, sizeof(msg));
	}
 }


/** @brief Directly removes a node from the slave list and stops the time source
 *         when the last slave is removed.
 *  @param node  The media_node to remove. */
void
BTimeSource::DirectRemoveMe(const media_node& node)
{
	CALLED();
	ASSERT(fSlaveNodes != NULL);
	BAutolock lock(fSlaveNodes);

	if (fSlaveNodes->CountSlaves() == 0) {
		ERROR("BTimeSource::DirectRemoveMe no slots used\n");
		return;
	}

	if (fSlaveNodes->RemoveSlave(node) != true) {
		ERROR("BTimeSource::DirectRemoveMe failed\n");
		return;
	}

	if (fSlaveNodes->CountSlaves() == 0) {
		// stop the time source
		time_source_op_info msg;
		msg.op = B_TIMESOURCE_STOP_IMMEDIATELY;
		msg.real_time = RealTime();

		TRACE_TIMESOURCE("stopping time source %" B_PRId32 "\n", ID());

		write_port(fControlPort, TIMESOURCE_OP, &msg, sizeof(msg));
	}
}


/** @brief Marks the time source as started in the shared-memory buffer.
 *  @param at  The real time at which the start takes effect (currently unused). */
void
BTimeSource::DirectStart(bigtime_t at)
{
	CALLED();
	if (fBuf)
		atomic_or(&fBuf->isrunning, 1);
	else
		fStarted = true;
}


/** @brief Marks the time source as stopped in the shared-memory buffer.
 *  @param at        The real time at which the stop takes effect (currently unused).
 *  @param immediate If \c true, stop immediately; otherwise schedule the stop. */
void
BTimeSource::DirectStop(bigtime_t at, bool immediate)
{
	CALLED();
	if (fBuf)
		atomic_and(&fBuf->isrunning, 0);
	else
		fStarted = false;
}


/** @brief Handles a direct seek request (not yet implemented).
 *  @param to  Target performance time.
 *  @param at  Real time at which the seek should occur. */
void
BTimeSource::DirectSeek(bigtime_t to, bigtime_t at)
{
	UNIMPLEMENTED();
}


/** @brief Handles a direct run-mode change (not yet implemented).
 *  @param mode  The new run mode. */
void
BTimeSource::DirectSetRunMode(run_mode mode)
{
	UNIMPLEMENTED();
}
