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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file InitRealTimeClockJob.h
 *  @brief Boot job that loads persisted RTC settings (GMT flag, timezone offset) at startup. */

#ifndef INIT_REAL_TIME_CLOCK_JOB_H
#define INIT_REAL_TIME_CLOCK_JOB_H


#include <Job.h>
#include <Path.h>


/** @brief Boot job that primes the kernel real-time clock from persisted user settings. */
class InitRealTimeClockJob : public BSupportKit::BJob {
public:
								InitRealTimeClockJob();

protected:
	/** @brief Loads the persisted RTC settings and applies them to the kernel. */
	virtual	status_t			Execute();

private:
			void				_SetRealTimeClockIsGMT(BPath path) const;
			void				_SetTimeZoneOffset(BPath path) const;
};


#endif // INIT_REAL_TIME_CLOCK_JOB_H
