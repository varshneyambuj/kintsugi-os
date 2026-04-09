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
 *   Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file KitSupport.h
 *  @brief Internal Bluetooth Kit helpers for server communication and inquiry timing. */

#ifndef KITSUPPORT_H
#define KITSUPPORT_H


#include <Messenger.h>


/** @brief Retrieves a BMessenger connected to the Bluetooth server.
 *  @return A heap-allocated BMessenger targeting the Bluetooth server,
 *          or NULL if the server is unreachable; caller takes ownership. */
BMessenger* _RetrieveBluetoothMessenger(void);

/** @brief Returns the currently configured inquiry duration in units of 1.28 s.
 *  @return The inquiry time value (1–48, corresponding to 1.28–61.44 s). */
uint8 GetInquiryTime();

/** @brief Sets the inquiry duration used by subsequent StartInquiry calls.
 *  @param time Inquiry duration in units of 1.28 s (valid range: BT_MIN_INQUIRY_TIME to BT_MAX_INQUIRY_TIME). */
void SetInquiryTime(uint8 time);


#endif
