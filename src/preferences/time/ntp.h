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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2004, pinc Software.
 */

/** @file ntp.h
    @brief Public entry point for the SNTP client used by NetworkTimeView. */

#ifndef NTP_H
#define NTP_H


#include <SupportDefs.h>


/**
 * @brief Performs a single SNTP exchange and updates the system clock.
 *
 * @param host        Hostname or IP of the NTP server.
 * @param errorString Output: human-readable error description on failure.
 * @param errorCode   Output: errno-style code on failure.
 * @return B_OK on success or an underlying error status.
 */
extern status_t ntp_update_time(const char *host,
	const char** errorString, int32* errorCode);


#endif	/* NTP_H */
