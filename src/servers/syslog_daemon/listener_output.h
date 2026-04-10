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
 *   Copyright 2003-2015, Axel Doerfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file listener_output.h
 *  @brief Interface for the BMessenger-based syslog listener output handler. */

#ifndef _LISTENER_OUTPUT_H_
#define _LISTENER_OUTPUT_H_


#include <Messenger.h>

#include "SyslogDaemon.h"


/** @brief Initialize the listener output handler and register it with the daemon. */
void init_listener_output(SyslogDaemon* daemon);
/** @brief Add a BMessenger as a syslog listener. */
void add_listener(BMessenger* messenger);
/** @brief Remove a previously added syslog listener. */
void remove_listener(BMessenger* messenger);


#endif	/* _LISTENER_OUTPUT_H_ */
