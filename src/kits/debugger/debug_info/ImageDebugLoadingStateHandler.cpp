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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ImageDebugLoadingStateHandler.cpp
 * @brief Implementation home for the abstract ImageDebugLoadingStateHandler
 *        base used to drive interactive debug-info loading flows.
 */


#include "ImageDebugLoadingStateHandler.h"


/**
 * @brief Virtual destructor for safe polymorphic deletion.
 */
ImageDebugLoadingStateHandler::~ImageDebugLoadingStateHandler()
{
}
