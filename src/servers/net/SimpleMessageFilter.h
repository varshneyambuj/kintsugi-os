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
   Copyright 2004, Waldemar Kornewald <wkornew@gmx.net>
   Distributed under the terms of the MIT License.
 */
/** @file SimpleMessageFilter.h
 *  @brief BMessageFilter subclass supporting multiple what-code filtering. */
#ifndef _SIMPLE_MESSAGE_FILTER__H
#define _SIMPLE_MESSAGE_FILTER__H

#include <MessageFilter.h>


/** @brief Message filter supporting multiple what-code values. */
class SimpleMessageFilter : public BMessageFilter {
	public:
		/** @brief Construct with a zero-terminated array of what values. */
		SimpleMessageFilter(const uint32 *what, BHandler *target);
		/** @brief Free the copied what-value array. */
		virtual ~SimpleMessageFilter();
		
		/** @brief Route matching messages to the target handler. */
		virtual filter_result Filter(BMessage *message, BHandler **target);

	private:
		uint32 *fWhatArray; /**< Zero-terminated array of filtered what values */
		BHandler *fTarget; /**< Handler receiving matched messages */
};


#endif
