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
 *   Copyright (c) 2003-2004, Marcus Overhagen <marcus@overhagen.de>
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without modification,
 *   are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @file SystemTimeSource.h
 *  @brief System-wide time source node that publishes real-time clock values. */

#ifndef __SYSTEM_TIME_SOURCE_H
#define __SYSTEM_TIME_SOURCE_H
 
#include <TimeSource.h>

/** @brief BTimeSource implementation backed by the system real-time clock. */
class SystemTimeSource : public BTimeSource
{
public:
	/** @brief Construct the system time source node. */
	SystemTimeSource();
	/** @brief Shut down the control thread and destroy the node. */
	~SystemTimeSource();

	/** @brief Start the control thread after the node is registered. */
	void NodeRegistered();

	/** @brief Return NULL since this node has no associated add-on. */
	BMediaAddOn* AddOn(int32 * internal_id) const;
	/** @brief Publish the current real time in response to time source operations. */
	status_t TimeSourceOp(const time_source_op_info & op, void * _reserved);


	static int32 _ControlThreadStart(void *arg);
	void ControlThread();

	thread_id fControlThread; /**< Thread ID of the control message loop */
};

#endif // __SYSTEM_TIME_SOURCE_H
