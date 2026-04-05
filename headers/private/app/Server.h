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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005-2015, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */
#ifndef _SERVER_H
#define _SERVER_H


#include <Application.h>


class BServer : public BApplication {
public:
								BServer(const char* signature, bool initGUI,
									status_t *error);
								BServer(const char* signature, const char*
									looperName, port_id port, bool initGUI,
									status_t *error);

			status_t			InitGUIContext();

private:
			bool				fGUIContextInitialized;
};


#endif	// _SERVER_H
