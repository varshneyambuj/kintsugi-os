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

/** @file SystemTimeSource.cpp
 *  @brief Implementation of the system-wide real-time clock time source node. */

#include "MediaDebug.h"
#include "SystemTimeSource.h"


/**
 * @brief Constructs the system-wide real-time clock time source node.
 *
 * Initializes the node with the name "System clock" and sets the
 * control thread ID to invalid.
 */
SystemTimeSource::SystemTimeSource()
 :	BMediaNode("System clock"),
 	BTimeSource(),
 	fControlThread(-1)
{
	TRACE("SystemTimeSource::SystemTimeSource\n");
}

/**
 * @brief Destroys the system time source, closing its control port and waiting for its thread.
 */
SystemTimeSource::~SystemTimeSource()
{
	TRACE("SystemTimeSource::~SystemTimeSource enter\n");
	
	close_port(ControlPort());
	if (fControlThread != -1) {
		status_t err;
		wait_for_thread(fControlThread, &err);
	}
	TRACE("SystemTimeSource::~SystemTimeSource exit\n");
}

/**
 * @brief Returns the add-on that instantiated this node.
 *
 * The system time source is not created by an add-on, so this always
 * returns NULL.
 *
 * @param internal_id Unused output parameter.
 * @return NULL always.
 */
BMediaAddOn*
SystemTimeSource::AddOn(int32 * internal_id) const
{
	return NULL;
}
	
/**
 * @brief Handles time source operations by publishing the current real time.
 *
 * @param op        The time source operation info (unused beyond triggering a publish).
 * @param _reserved Reserved parameter (unused).
 * @return B_OK always.
 */
status_t
SystemTimeSource::TimeSourceOp(const time_source_op_info & op, void * _reserved)
{
	TRACE("######## SystemTimeSource::TimeSourceOp\n");
	bigtime_t real = RealTime();
	PublishTime(real, real, 1.0);
	return B_OK;
}


/**
 * @brief Called when the node is registered; spawns the control thread.
 */
/* virtual */ void
SystemTimeSource::NodeRegistered()
{
	ASSERT(fControlThread == -1);
	fControlThread = spawn_thread(_ControlThreadStart, "System clock control", 12, this);
	resume_thread(fControlThread);
}

/**
 * @brief Static thread entry point that delegates to ControlThread().
 *
 * @param arg Pointer to the SystemTimeSource instance.
 * @return 0 always.
 */
int32
SystemTimeSource::_ControlThreadStart(void *arg)
{
	reinterpret_cast<SystemTimeSource *>(arg)->ControlThread();
	return 0;
}

/**
 * @brief Control thread loop that processes incoming media node messages.
 *
 * Calls WaitForMessage() in an infinite loop until the port is deleted
 * or an unrecoverable error occurs.
 */
void
SystemTimeSource::ControlThread()
{
	TRACE("SystemTimeSource::ControlThread() enter\n");
	status_t err;
	do {
		err = WaitForMessage(B_INFINITE_TIMEOUT);
	} while (err == B_OK || err == B_ERROR);
	TRACE("SystemTimeSource::ControlThread() exit\n");
}
