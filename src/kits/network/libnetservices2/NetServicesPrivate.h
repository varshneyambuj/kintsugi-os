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
 * MIT License. Copyright 2022, Haiku Inc.
 * Original author: Niels Sascha Reedijk.
 */

/** @file NetServicesPrivate.h
    @brief Private hooks shared by the libnetservices2 implementation, such
           as the monotonic identifier allocator used to tag in-flight
           requests. */

#ifndef _NET_SERVICES_PRIVATE_H_
#define _NET_SERVICES_PRIVATE_H_


namespace BPrivate {

namespace Network {


/** @brief Returns a process-unique monotonically increasing request id. */
int32 get_netservices_request_identifier();


} // namespace Network

} // namespace BPrivate

#endif // _NET_SERVICES_PRIVATE_H
