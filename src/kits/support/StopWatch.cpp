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
 *   Copyright (c) 2001-2004, Haiku
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"),
 *   to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *   and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *   DEALINGS IN THE SOFTWARE.
 */


/**
 * @file StopWatch.cpp
 * @brief High-resolution elapsed-time timer with lap recording.
 *
 * BStopWatch wraps system_time() to provide a simple microsecond-resolution
 * stopwatch. It supports suspension, resumption, up to nine lap marks, and
 * optional automatic reporting to stdout on destruction. Primarily intended
 * for debugging and profiling.
 *
 * @see BStopWatch
 */


#include <OS.h>		// for system_time()
#include <StopWatch.h>

#include <stdio.h>


/**
 * @brief Construct and immediately start the stopwatch.
 *
 * Calls Reset() to record the start time, zeroes any existing lap data,
 * and stores the display options.
 *
 * @param name   Human-readable label used in the optional destructor
 *               printout. May be NULL.
 * @param silent When true, the destructor does not print elapsed time or
 *               lap data to stdout.
 */
BStopWatch::BStopWatch(const char *name, bool silent)
	:
	fName(name),
	fSilent(silent)
{
	Reset();
}


/**
 * @brief Destructor. Optionally prints elapsed time and lap data to stdout.
 *
 * If \a silent was false at construction, prints the total elapsed time in
 * microseconds. If any laps were recorded, their absolute and delta times
 * are printed four per line.
 */
BStopWatch::~BStopWatch()
{
	if (!fSilent){
		printf("StopWatch \"%s\": %d usecs.\n", fName, (int)ElapsedTime());

		if (fLap) {
			for (int i = 1; i <= fLap; i++){
				if (!((i-1)%4))
					printf("\n   ");
				printf("[%d: %d#%d] ", i, (int)(fLaps[i]-fStart), (int)(fLaps[i] - fLaps[i -1 ]));
			}
			printf("\n");
		}
	}
}


/**
 * @brief Suspend the stopwatch, freezing the elapsed time.
 *
 * Records the current system_time() as the suspend timestamp. Subsequent
 * calls to ElapsedTime() will return the time at which Suspend() was called
 * rather than the live clock. Has no effect if already suspended.
 *
 * @see Resume(), ElapsedTime()
 */
void
BStopWatch::Suspend()
{
	if (!fSuspendTime)
		fSuspendTime = system_time();
}


/**
 * @brief Resume the stopwatch after a Suspend().
 *
 * Advances the logical start time by the duration of the suspension so
 * that ElapsedTime() continues as if the suspended interval never occurred.
 * Has no effect if the stopwatch is not currently suspended.
 *
 * @see Suspend(), ElapsedTime()
 */
void
BStopWatch::Resume()
{
	if (fSuspendTime) {
		fStart += system_time() - fSuspendTime;
		fSuspendTime = 0;
	}
}


/**
 * @brief Record a lap mark and return the total elapsed time.
 *
 * Stores the current system_time() as the next lap timestamp (up to a
 * maximum of 9 laps; additional calls are silently ignored beyond that
 * limit). Returns 0 if the stopwatch is currently suspended.
 *
 * @return Total elapsed time in microseconds from the last Reset() to
 *         the moment this lap was recorded, or 0 if suspended.
 * @see ElapsedTime(), Reset()
 */
bigtime_t
BStopWatch::Lap()
{
	if (!fSuspendTime){
		if (fLap<9)
			fLap++;
		fLaps[fLap] = system_time();

		return system_time() - fStart;
	} else
		return 0;
}


/**
 * @brief Return the total elapsed time in microseconds.
 *
 * If the stopwatch is running, returns the interval between the last
 * Reset() and now. If suspended, returns the interval between the last
 * Reset() and the moment Suspend() was called.
 *
 * @return Elapsed time in microseconds.
 * @see Suspend(), Reset()
 */
bigtime_t
BStopWatch::ElapsedTime() const
{
	if (fSuspendTime)
		return fSuspendTime - fStart;
	else
		return system_time() - fStart;
}


/**
 * @brief Reset the stopwatch, clearing all lap data and restarting the timer.
 *
 * Records the current system_time() as the new start time, clears the
 * suspend timestamp, and zeroes all lap slots. After this call, the
 * stopwatch is in the running (not suspended) state.
 *
 * @see Lap(), ElapsedTime()
 */
void
BStopWatch::Reset()
{
	fStart = system_time();		// store current time
	fSuspendTime = 0;
	fLap = 0;					// clear laps
	for (int i = 0; i < 10; i++)
		fLaps[i] = fStart;
}


/**
 * @brief Return the name assigned to this stopwatch.
 *
 * @return The name string passed to the constructor, or an empty string
 *         if NULL was passed.
 */
const char *
BStopWatch::Name() const
{
	return fName != NULL ? fName : "";
}


// just for future binary compatibility
void BStopWatch::_ReservedStopWatch1()	{}
void BStopWatch::_ReservedStopWatch2()	{}
