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
 *   Copyright 2015, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SignalInfo.cpp
 * @brief Implementation of SignalInfo, describing one POSIX signal observed
 *        by the debugger (signal number, installed handler, deadliness).
 *
 * SignalInfo is reported when the debugged team is about to deliver a
 * signal; the captured sigaction lets the UI display the handler that
 * would run, and @c fDeadly marks signals whose default action would
 * terminate the team.
 */

#include "SignalInfo.h"

#include <string.h>


/**
 * @brief Constructs an empty SignalInfo with signal 0 and a zeroed handler.
 */
SignalInfo::SignalInfo()
	:
	fSignal(0),
	fDeadly(false)
{
	memset(&fHandler, 0, sizeof(fHandler));
}


/**
 * @brief Copy-constructs from another SignalInfo, duplicating the sigaction.
 *
 * @param other Source instance to copy.
 */
SignalInfo::SignalInfo(const SignalInfo& other)
	:
	fSignal(other.fSignal),
	fDeadly(other.fDeadly)
{
	memcpy(&fHandler, &other.fHandler, sizeof(fHandler));
}


/**
 * @brief Constructs a SignalInfo from explicit signal data.
 *
 * @param signal  POSIX signal number being delivered.
 * @param handler Currently installed sigaction for @a signal.
 * @param deadly  True if delivery would terminate the team by default.
 */
SignalInfo::SignalInfo(int signal, const struct sigaction& handler,
	bool deadly)
	:
	fSignal(signal),
	fDeadly(deadly)
{
	memcpy(&fHandler, &handler, sizeof(fHandler));
}


/**
 * @brief Replaces the captured signal data with new values.
 *
 * @param signal  POSIX signal number being delivered.
 * @param handler Currently installed sigaction for @a signal.
 * @param deadly  True if delivery would terminate the team by default.
 */
void
SignalInfo::SetTo(int signal, const struct sigaction& handler, bool deadly)
{
	fSignal = signal;
	fDeadly = deadly;

	memcpy(&fHandler, &handler, sizeof(fHandler));
}
