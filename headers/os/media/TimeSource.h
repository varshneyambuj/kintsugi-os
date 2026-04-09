/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file TimeSource.h
 *  @brief Defines BTimeSource, the base class for media time-source nodes.
 */

#ifndef _TIME_SOURCE_H
#define _TIME_SOURCE_H


#include <MediaDefs.h>
#include <MediaNode.h>


class _BSlaveNodeStorageP;

namespace BPrivate {
	namespace media {
		class BMediaRosterEx;
		class TimeSourceObject;
		class SystemTimeSourceObject;
		class SlaveNodes;
		struct TimeSourceTransmit;
	}
}


/** @brief Base class for nodes that provide a performance-time clock to slave nodes.
 *
 *  A BTimeSource maps real (wall-clock) time to performance time with an
 *  optional drift factor.  Slave nodes query it via PerformanceTimeFor() and
 *  RealTimeFor() to stay synchronized.  Subclass BTimeSource and implement
 *  TimeSourceOp() to handle start/stop/seek requests.
 */
class BTimeSource : public virtual BMediaNode {
protected:
	virtual						~BTimeSource();

public:
	/** @brief Sleeps until the given performance time arrives (with optional latency guard).
	 *  @param performanceTime Target performance time.
	 *  @param withLatency Extra safety margin in microseconds.
	 *  @param retrySignals If true, retry after interrupted sleep.
	 *  @return B_OK when the time arrives, or an error code.
	 */
	virtual	status_t			SnoozeUntil(bigtime_t performanceTime,
									bigtime_t withLatency = 0,
									bool retrySignals = false);

	/** @brief Returns the current performance time.
	 *  @return Current performance time in microseconds.
	 */
			bigtime_t			Now();

	/** @brief Converts a real time to the corresponding performance time.
	 *  @param realTime The real time to convert.
	 *  @return The corresponding performance time.
	 */
			bigtime_t			PerformanceTimeFor(bigtime_t realTime);

	/** @brief Converts a performance time to the corresponding real time.
	 *  @param performanceTime The performance time to convert.
	 *  @param withLatency Extra latency to add to the result.
	 *  @return The corresponding real time.
	 */
			bigtime_t			RealTimeFor(bigtime_t performanceTime,
									bigtime_t withLatency);

	/** @brief Returns true if this time source is currently running.
	 *  @return True if running.
	 */
			bool				IsRunning();

	/** @brief Returns the current performance time, real time, and drift factor.
	 *  @param _performanceTime On return, the current performance time.
	 *  @param _realTime On return, the corresponding real time.
	 *  @param _drift On return, the current drift factor (1.0 = normal speed).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetTime(bigtime_t* _performanceTime,
									bigtime_t* _realTime, float* _drift);

	/** @brief Returns the latency between a start request and actual start.
	 *  @param _latency On return, the start latency in microseconds.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetStartLatency(bigtime_t* _latency);

	/** @brief Returns the current real (wall-clock) time.
	 *  @return Real time in microseconds.
	 */
	static	bigtime_t			RealTime();

protected:
	/** @brief Default constructor. */
								BTimeSource();

	/** @brief Dispatches an incoming port message to the appropriate handler.
	 *  @param message The message code.
	 *  @param data Pointer to the message payload.
	 *  @param size Size of the payload in bytes.
	 *  @return B_OK if handled, or an error code.
	 */
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	/** @brief Publishes a new time mapping for slave nodes to use.
	 *  @param performanceTime Current performance time.
	 *  @param realTime Corresponding real time.
	 *  @param drift Current drift factor.
	 */
			void				PublishTime(bigtime_t performanceTime,
									bigtime_t realTime, float drift);

	/** @brief Notifies slave nodes of a sudden jump in performance time.
	 *  @param atRealTime Real time at which the warp takes effect.
	 *  @param newPerformanceTime New performance time value after the warp.
	 */
			void				BroadcastTimeWarp(bigtime_t atRealTime,
									bigtime_t newPerformanceTime);

	/** @brief Sends the current run mode to slave nodes.
	 *  @param mode The new run_mode value.
	 */
			void				SendRunMode(run_mode mode);

	/** @brief Called when the run mode of this time source changes. */
	virtual	void				SetRunMode(run_mode mode);

	/** @brief Operation codes for TimeSourceOp(). */
	enum time_source_op {
		B_TIMESOURCE_START = 1,          /**< Start the time source. */
		B_TIMESOURCE_STOP,               /**< Stop the time source. */
		B_TIMESOURCE_STOP_IMMEDIATELY,   /**< Stop without waiting. */
		B_TIMESOURCE_SEEK                /**< Seek to a new performance time. */
	};

	/** @brief Carries the parameters of a time-source control operation. */
	struct time_source_op_info {
		time_source_op	op;               /**< The operation to perform. */
		int32			_reserved1;
		bigtime_t		real_time;        /**< Real time at which the op takes effect. */
		bigtime_t		performance_time; /**< Performance time for seek operations. */
		int32			_reserved2[6];
	};

	/** @brief Called to handle a start, stop, or seek operation.
	 *  @param op The operation descriptor.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			TimeSourceOp(const time_source_op_info& op,
									void* _reserved) = 0;

private:
	friend class BMediaNode;
	friend class BMediaRoster;
	friend class BPrivate::media::BMediaRosterEx;
	friend class BPrivate::media::SystemTimeSourceObject;
	friend class BPrivate::media::TimeSourceObject;

								BTimeSource(const BTimeSource& other);
			BTimeSource&		operator=(const BTimeSource& other);
									// not implemented

	explicit					BTimeSource(media_node_id id);

			status_t			_Reserved_TimeSource_0(void*);
				// used for TimeSourceOp()
	virtual	status_t			_Reserved_TimeSource_1(void*);
	virtual	status_t			_Reserved_TimeSource_2(void*);
	virtual	status_t			_Reserved_TimeSource_3(void*);
	virtual	status_t			_Reserved_TimeSource_4(void*);
	virtual	status_t			_Reserved_TimeSource_5(void*);

	virtual	status_t			RemoveMe(BMediaNode* node);
	virtual	status_t			AddMe(BMediaNode* node);

			void				FinishCreate();

			void				DirectStart(bigtime_t at);
			void				DirectStop(bigtime_t at, bool immediate);
			void				DirectSeek(bigtime_t to, bigtime_t at);
			void				DirectSetRunMode(run_mode mode);
			void				DirectAddMe(const media_node& node);
			void				DirectRemoveMe(const media_node& node);

private:
			bool				fStarted;
			area_id				fArea;
			BPrivate::media::TimeSourceTransmit* fBuf;
			BPrivate::media::SlaveNodes* fSlaveNodes;

			area_id				_reserved_area;
			bool				fIsRealtime;
			bool				_reserved_bool_[3];
			uint32				_reserved_time_source_[10];
};


#endif	// _TIME_SOURCE_H
